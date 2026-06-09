#include "query.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <string>

#include "component/interface.h"
#include "component/network.h"
#include "util/time.h"

#include "protocol.h"

static constexpr double QUERY_TIMEOUT = 2.5;
static constexpr double QUERY_REFRESH = 3.0;

static auto status_key(const ServerEntry& entry) -> std::string {
    return entry.address + ":" + std::to_string(entry.port);
}

static void start_queries(flecs::world world) {
    auto& query = world.get_mut<NetworkQueryState>();
    if (query.socket == ENET_SOCKET_NULL) {
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
            board.by_address[key] = {.state = ServerStatus::State::Offline};
            continue;
        }
        address.port = entry.port;

        uint64_t token = query.next_token++;
        query.pending[token] = {.key = key, .start = util::now(), .host = address.host};

        serialize::Writer w;
        w.put<uint32_t>(QUERY_MAGIC);
        MessagePing ping{.token = token};
        serialize::encode(w, ping);
        ENetBuffer buffer{.data = w.data.data(), .dataLength = w.data.size()};
        enet_socket_send(query.socket, &address, &buffer, 1);

        ServerStatus& status = board.by_address[key];
        if (status.state == ServerStatus::State::Unknown) {
            status.state = ServerStatus::State::Querying;
        }
    }
}

NetworkQuery::NetworkQuery(flecs::world& world) {
    world.component<NetworkQueryState>().add(flecs::Singleton);
    world.component<ServerStatusBoard>().add(flecs::Singleton);

    ENetSocket socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (socket == ENET_SOCKET_NULL) {
        SDL_Log("query: failed to create query socket");
    } else {
        enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1);
        ENetAddress any{.host = ENET_HOST_ANY, .port = 0};
        enet_socket_bind(socket, &any);
    }
    world.set<NetworkQueryState>({.socket = socket});
    world.set<ServerStatusBoard>({});

    world.system<NetworkQueryState>("query::pump").kind(flecs::PreFrame).each(NetworkQuery::pump);
}

void NetworkQuery::pump(flecs::iter& it, size_t, NetworkQueryState& query) {
    if (query.socket == ENET_SOCKET_NULL) {
        return;
    }
    flecs::world world = it.world();
    auto& board = world.get_mut<ServerStatusBoard>();

    std::array<uint8_t, 512> buf{};
    ENetAddress from{};
    for (;;) {
        ENetBuffer buffer{.data = buf.data(), .dataLength = buf.size()};
        int n = enet_socket_receive(query.socket, &from, &buffer, 1);
        if (n <= 0) {
            break;
        }
        serialize::Reader r(buf.data(), static_cast<size_t>(n));
        if (r.get<uint32_t>() != QUERY_MAGIC) {
            continue;
        }
        auto pong = serialize::decode<MessagePong>(r);
        if (!r.valid()) {
            continue;
        }
        auto hit = query.pending.find(pong.token);
        if (hit == query.pending.end() || hit->second.host != from.host) {
            continue;
        }
        ServerStatus status;
        status.state = (pong.protocol == NETWORK_PROTOCOL) ? ServerStatus::State::Online : ServerStatus::State::Incompatible;
        status.players = pong.players;
        status.max = pong.max_players;
        status.ping = static_cast<uint16_t>(std::min(9999.0, (util::now() - hit->second.start) * 1000.0));
        board.by_address[hit->second.key] = status;
        query.pending.erase(hit);
    }

    double now = util::now();
    for (auto i = query.pending.begin(); i != query.pending.end();) {
        if (now - i->second.start > QUERY_TIMEOUT) {
            board.by_address[i->second.key].state = ServerStatus::State::Offline;
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
