#include "query.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>

#include "component/interface.h"
#include "component/network.h"
#include "util/time.h"

#include "protocol.h"

static constexpr double QUERY_TIMEOUT = 2.5;
static constexpr double QUERY_REFRESH = 3.0;
static constexpr int MAX_QUERY_PEERS = 32;

static auto status_key(const ServerEntry& entry) -> std::string {
    return entry.address + ":" + std::to_string(entry.port);
}

static void start_queries(flecs::world world) {
    auto& query = world.get_mut<NetworkQueryState>();
    if (query.host == nullptr) {
        return;
    }
    auto& board = world.get_mut<ServerStatusBoard>();
    const auto* list = world.try_get<ServerList>();
    if (list == nullptr) {
        return;
    }
    query.last_query = util::now();

    for (const auto& entry : list->entries) {
        std::string key = status_key(entry);
        ENetAddress address{};
        if (enet_address_set_host(&address, entry.address.c_str()) != 0) {
            board.byAddress[key] = {.state = ServerStatus::State::Offline};
            continue;
        }
        address.port = entry.port;
        ENetPeer* peer = enet_host_connect(query.host, &address, CHANNEL_COUNT, 0);
        if (peer == nullptr) {
            board.byAddress[key] = {.state = ServerStatus::State::Offline};
            continue;
        }
        query.pending[peer] = {.key = key, .start = util::now(), .sent = 0, .token = query.next_token++};
        ServerStatus& status = board.byAddress[key];
        if (status.state == ServerStatus::State::Unknown) {
            status.state = ServerStatus::State::Querying;
        }
    }
}

NetworkQuery::NetworkQuery(flecs::world& world) {
    world.component<NetworkQueryState>().add(flecs::Singleton);
    world.component<ServerStatusBoard>().add(flecs::Singleton);

    ENetHost* host = enet_host_create(nullptr, MAX_QUERY_PEERS, CHANNEL_COUNT, 0, 0);
    if (host == nullptr) {
        SDL_Log("query: failed to create query host");
    }
    world.set<NetworkQueryState>({.host = host});
    world.set<ServerStatusBoard>({});

    world.system<NetworkQueryState>("query::pump").kind(flecs::PreFrame).each(NetworkQuery::pump);
}

void NetworkQuery::pump(flecs::iter& it, size_t, NetworkQueryState& query) {
    if (query.host == nullptr) {
        return;
    }
    flecs::world world = it.world();
    auto& board = world.get_mut<ServerStatusBoard>();

    ENetEvent ev;
    while (enet_host_service(query.host, &ev, 0) > 0) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                auto hit = query.pending.find(ev.peer);
                if (hit != query.pending.end()) {
                    hit->second.sent = util::now();
                    Writer w = wire::message(Message::Ping);
                    MessagePing ping{.token = hit->second.token};
                    util::encode(w, ping);
                    wire::send(ev.peer, w, CHANNEL_RELIABLE, true);
                }
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                Reader r(ev.packet->data, ev.packet->dataLength);
                auto kind = static_cast<Message>(r.get<uint8_t>());
                auto hit = query.pending.find(ev.peer);
                if (kind == Message::Pong && hit != query.pending.end()) {
                    auto pong = util::decode<MessagePong>(r);
                    ServerStatus status;
                    status.state = (pong.protocol == NETWORK_PROTOCOL) ? ServerStatus::State::Online : ServerStatus::State::Incompatible;
                    status.players = pong.players;
                    status.max = pong.max_players;
                    double base = hit->second.sent > 0 ? hit->second.sent : hit->second.start;
                    status.ping = static_cast<uint16_t>(std::min(9999.0, (util::now() - base) * 1000.0));
                    board.byAddress[hit->second.key] = status;
                    enet_peer_disconnect(ev.peer, 0);
                    query.pending.erase(hit);
                }
                enet_packet_destroy(ev.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                auto hit = query.pending.find(ev.peer);
                if (hit != query.pending.end()) {
                    board.byAddress[hit->second.key].state = ServerStatus::State::Offline;
                    query.pending.erase(hit);
                }
                break;
            }
            default:
                break;
        }
    }

    double now = util::now();
    for (auto i = query.pending.begin(); i != query.pending.end();) {
        if (now - i->second.start > QUERY_TIMEOUT) {
            board.byAddress[i->second.key].state = ServerStatus::State::Offline;
            enet_peer_reset(i->first);
            i = query.pending.erase(i);
        } else {
            ++i;
        }
    }

    const auto* page = world.try_get<InterfacePage>();
    if (page != nullptr && *page == InterfacePage::Connect && now - query.last_query > QUERY_REFRESH) {
        start_queries(world);
    }
}
