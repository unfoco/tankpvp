#include "server.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "component/interface.h"
#include "component/input.h"
#include "component/network.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/script.h"
#include "util/math.h"
#include "network.h"
#include "protocol.h"
#include "registry.h"
#include "snapshot.h"


static auto peer_entity(flecs::world& world, ENetPeer* peer) -> flecs::entity {
    return world.entity(static_cast<flecs::entity_t>(reinterpret_cast<uintptr_t>(peer->data)));
}

static void kick(ENetPeer* peer, const std::string& reason) {
    serialize::Writer w = wire::message(Message::Kick);
    MessageKick msg{reason};
    serialize::encode(w, msg);
    wire::send(peer, w, CHANNEL_RELIABLE, true);
    enet_peer_disconnect_later(peer, 0);
}

static void broadcast_chat(flecs::world& world, const std::string& line) {
    serialize::Writer w = wire::message(Message::Chat);
    MessageChat out{line};
    serialize::encode(w, out);
    world.query_builder<Peer>().build().each([&](const Peer& p) {
        if (p.welcomed && (p.peer != nullptr)) {
            wire::send(p.peer, w, CHANNEL_RELIABLE, true);
        }
    });
    if (ChatLog* log = world.try_get_mut<ChatLog>()) {
        log->push(line);
    }
}

static void send_chat(flecs::world& world, flecs::entity peer_entity, const std::string& line) {
    if (peer_entity && peer_entity.is_alive() && peer_entity.has<Peer>()) {
        const Peer& p = peer_entity.get<Peer>();
        if (p.peer != nullptr) {
            serialize::Writer w = wire::message(Message::Chat);
            MessageChat out{line};
            serialize::encode(w, out);
            wire::send(p.peer, w, CHANNEL_RELIABLE, true);
            return;
        }
    }
    if (ChatLog* log = world.try_get_mut<ChatLog>()) {
        log->push(line);
    }
}

static auto peer_socket(flecs::entity peer_entity) -> ENetPeer* {
    if (peer_entity && peer_entity.is_alive() && peer_entity.has<Peer>()) {
        return peer_entity.get<Peer>().peer;
    }
    return nullptr;
}

static auto to_message_widget(const ViewWidget& w) -> MessageViewWidget {
    MessageViewWidget out;
    out.kind = static_cast<uint8_t>(w.kind);
    out.layout = static_cast<uint8_t>(w.layout);
    out.text = w.text;
    out.handler = w.handler;
    out.bind = w.bind;
    out.number = w.number;
    out.bind_max = w.bind_max;
    out.number_max = w.number_max;
    out.color_r = w.color_r;
    out.color_g = w.color_g;
    out.color_b = w.color_b;
    out.bg_r = w.bg_r;
    out.bg_g = w.bg_g;
    out.bg_b = w.bg_b;
    out.bg_a = w.bg_a;
    out.field = w.field;
    for (const auto& c : w.children) {
        out.children.push_back(to_message_widget(c));
    }
    return out;
}

static void deliver_ui(flecs::world& world, const RequestView& req) {
    if (ENetPeer* socket = peer_socket(req.peer); socket != nullptr) {
        if (req.close) {
            serialize::Writer w = wire::message(Message::ViewClose);
            MessageViewClose msg{.id = req.id};
            serialize::encode(w, msg);
            wire::send(socket, w, CHANNEL_RELIABLE, true);
        } else {
            serialize::Writer w = wire::message(Message::ViewOpen);
            MessageViewOpen msg{.id = req.id, .placement = static_cast<uint8_t>(req.placement), .root = to_message_widget(req.root)};
            serialize::encode(w, msg);
            wire::send(socket, w, CHANNEL_RELIABLE, true);
        }
        return;
    }
    auto* view = world.try_get_mut<ViewState>();
    if (view == nullptr) {
        return;
    }
    std::erase_if(view->views, [&](const ViewActive& s) -> bool { return s.id == req.id; });
    if (!req.close) {
        view->views.push_back({.id = req.id, .placement = req.placement, .root = req.root});
        if (req.placement == ViewPlacement::Center) {
            if (auto* page = world.try_get<InterfacePage>(); page != nullptr && *page == InterfacePage::Chat) {
                world.set(InterfacePage::Ingame);
            }
        }
    }
}

struct ServerQueries {
    flecs::query<Peer> inputs;
    flecs::query<const Position, const Rotation, History> record;
    flecs::query<const Position, const Owner> bullets;
    flecs::query<const History, const CollisionBox> tanks;
    flecs::query<const NetworkId, const History, const CollisionBox> tanks_by_id;
    flecs::query<> unassigned;
    flecs::query<const NetworkId> replicated;
    flecs::query<Peer, Interest, Replication> peers;
};

NetworkServer::NetworkServer(flecs::world& world) {
    world.system("network::server::pump").kind(flecs::OnLoad).immediate().run(NetworkServer::pump);
    flecs::entity post_physics = world.lookup("physics::Post");
    flecs::entity hits_phase = world.entity("network::Hits").add(flecs::Phase).depends_on(post_physics ? post_physics : world.entity(flecs::PostUpdate));
    world.system("network::server::hits").kind(hits_phase).immediate().run(NetworkServer::hits);
    world.system("network::server::replicate").kind(flecs::OnStore).immediate().run(NetworkServer::replicate);

    world.set<ServerClock>({});

    world.observer<const RequestBroadcast>("network::server::chat_broadcast").event(flecs::OnSet).each([](flecs::entity e, const RequestBroadcast& c) -> void {
        flecs::world world = e.world();
        broadcast_chat(world, c.line);
        e.destruct();
    });
    world.observer<const RequestReply>("network::server::chat_reply").event(flecs::OnSet).each([](flecs::entity e, const RequestReply& c) -> void {
        flecs::world world = e.world();
        send_chat(world, c.peer, c.line);
        e.destruct();
    });
    world.observer<const RequestView>("network::server::view").event(flecs::OnSet).each([](flecs::entity e, const RequestView& req) -> void {
        flecs::world world = e.world();
        deliver_ui(world, req);
        e.destruct();
    });

    world.set<ServerQueries>({
        .inputs = world.query_builder<Peer>().build(),
        .record = world.query_builder<const Position, const Rotation, History>().with<Tank>().build(),
        .bullets = world.query_builder<const Position, const Owner>().with<Bullet>().build(),
        .tanks = world.query_builder<const History, const CollisionBox>().with<Tank>().build(),
        .tanks_by_id = world.query_builder<const NetworkId, const History, const CollisionBox>().with<Tank>().build(),
        .unassigned = world.query_builder().with<Replicated>().without<NetworkId>().build(),
        .replicated = world.query_builder<const NetworkId>().with<Replicated>().build(),
        .peers = world.query_builder<Peer, Interest, Replication>().build(),
    });
}

void NetworkServer::teardown(flecs::world& world) {
    world.defer([&] -> void { world.query_builder().with<Peer>().or_().with<Replicated>().build().each([](flecs::entity e) -> void { e.destruct(); }); });
}

static auto to_message_command(const CommandInfo& info) -> MessageCommandInfo {
    MessageCommandInfo entry;
    entry.name = info.name;
    entry.description = info.description;
    for (const auto& arg : info.arguments) {
        entry.arguments.push_back({.name = arg.name, .type = arg.type, .optional = static_cast<uint8_t>(arg.optional ? 1 : 0), .values = arg.values});
    }
    for (const auto& sub : info.subcommands) {
        entry.subcommands.push_back(to_message_command(sub));
    }
    return entry;
}

static void send_welcome(flecs::world& world, NetworkHost& host, ENetPeer* peer, uint32_t pid, uint64_t entity) {
    MessageWelcome msg;
    msg.protocol = NETWORK_PROTOCOL;
    msg.peer_id = pid;
    msg.controlled_entity = entity;
    msg.tick = host.tick;
    msg.tickrate = host.tickrate;
    msg.components = world.get<NetworkRegistry>().describe();

    serialize::Writer w = wire::message(Message::Welcome);
    serialize::encode(w, msg);
    wire::send(peer, w, CHANNEL_RELIABLE, true);

    MessageCommandList list;
    if (const auto* book = world.try_get<CommandBook>()) {
        for (const auto& info : book->commands) {
            list.commands.push_back(to_message_command(info));
        }
    }
    serialize::Writer cw = wire::message(Message::CommandList);
    serialize::encode(cw, list);
    wire::send(peer, cw, CHANNEL_RELIABLE, true);
}

static void on_hello(flecs::world& world, NetworkHost& host, ENetPeer* epeer, const std::string& username) {
    std::string name = username.substr(0, 16);
    if (name.find(' ') != std::string::npos) {
        kick(epeer, "Username cannot contain spaces");
        return;
    }

    flecs::entity existing = peer_entity(world, epeer);
    if (existing && existing.is_alive() && existing.has<Peer>()) {
        existing.get_mut<Peer>().username = name;
        return;
    }

    uint32_t pid = host.next_peer++;

    flecs::entity tank = world.entity()
                             .set(Color{.value = {50 + ((pid * 53) % 205), 50 + ((pid * 97) % 205), 50 + ((pid * 151) % 205)}})
                             .set(Position{.value = {300.0F + (static_cast<float>(pid % 8) * 70.0F), 300.0F}})
                             .set(Rotation{.angle = 0})
                             .set(VelocityLinear{})
                             .set(VelocityAngular{})
                             .set(CollisionBox{.height = 30, .width = 40})
                             .set(MovementStats{})
                             .set(WeaponStats{})
                             .add<InputFlags>()
                             .add<Dynamic>()
                             .add<Tank>()
                             .set(Firing{})
                             .add<History>()
                             .add<ViewLag>()
                             .set(Owner{.peer = pid});
    uint64_t nid = host.next_id++;
    tank.set<NetworkId>({nid}).add<Replicated>();

    flecs::entity pe = world.entity().set<Peer>({.peer = epeer, .id = pid, .username = name}).add<Interest>().add<Replication>();
    pe.add<Controls>(tank);

    epeer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(pe.id()));
    send_welcome(world, host, epeer, pid, nid);
    SDL_Log("network: peer %u ('%s') connected (entity %llu)", pid, name.c_str(), static_cast<unsigned long long>(nid));
}

static void on_disconnect(flecs::world& world, ENetPeer* epeer) {
    flecs::entity pe = peer_entity(world, epeer);
    if (pe && pe.is_alive()) {
        flecs::entity tank = pe.target<Controls>();
        if (tank && tank.is_alive()) {
            tank.destruct();
        }
        SDL_Log("network: peer %u disconnected", pe.has<Peer>() ? pe.get<Peer>().id : 0);
        pe.destruct();
    }
    epeer->data = nullptr;
}

static void handle_packet(flecs::world& world, ENetPeer* epeer, ENetPacket* packet) {
    serialize::Reader r(packet->data, packet->dataLength);
    auto kind = static_cast<Message>(r.get<uint8_t>());
    if (kind == Message::Hello) {
        auto hello = serialize::decode<MessageHello>(r);
        if (r.valid()) {
            on_hello(world, world.get_mut<NetworkHost>(), epeer, hello.username);
        }
        return;
    }
    if (kind == Message::Ping) {
        auto ping = serialize::decode<MessagePing>(r);
        if (!r.valid()) {
            return;
        }
        serialize::Writer w = wire::message(Message::Pong);
        MessagePong pong{
            .protocol = NETWORK_PROTOCOL,
            .token = ping.token,
            .players = static_cast<uint16_t>(world.count<Peer>()),
            .max_players = MAX_PLAYERS,
            .tickrate = world.get<NetworkHost>().tickrate,
        };
        serialize::encode(w, pong);
        wire::send(epeer, w, CHANNEL_RELIABLE, true);
        enet_peer_disconnect_later(epeer, 0);
        return;
    }
    flecs::entity pe = peer_entity(world, epeer);
    if (!pe || !pe.is_alive() || !pe.has<Peer>()) {
        return;
    }

    switch (kind) {
        case Message::Input: {
            Peer& p = pe.get_mut<Peer>();
            auto in = serialize::decode<MessageInput>(r);
            if (!r.valid()) {
                break;
            }
            p.stamp = std::max(in.send_time, p.stamp);
            for (const auto& c : in.commands) {
                if (c.tick_delta > in.newest_tick) {
                    continue;
                }
                uint64_t tick = in.newest_tick - c.tick_delta;
                if (tick <= p.simulated || (static_cast<unsigned int>(p.inputs.contains(tick)) != 0U)) {
                    continue;
                }
                p.inputs[tick] = {.tick = tick, .flags = c.flags, .prediction = c.prediction, .view = c.view, .muzzle = {c.muzzle_x, c.muzzle_y}, .aim = c.aim};
            }
            while (p.inputs.size() > 128) {
                p.inputs.erase(p.inputs.begin());
            }
            break;
        }
        case Message::Hit: {
            Peer& p = pe.get_mut<Peer>();
            auto hit = serialize::decode<MessageHit>(r);
            if (!r.valid()) {
                break;
            }
            for (const auto& c : hit.claims) {
                p.claims[c.prediction] = c.target;
            }
            break;
        }
        case Message::Ack: {
            auto ackMsg = serialize::decode<MessageAcknowledge>(r);
            if (!r.valid()) {
                break;
            }
            uint64_t ack = ackMsg.tick;
            Peer& p = pe.get_mut<Peer>();
            if (!p.welcomed) {
                p.welcomed = true;
                world.entity().set(RequestPlayerJoin{.peer = pe, .username = p.username});
            }
            if (pe.has<Replication>()) {
                auto& repl = pe.get_mut<Replication>();
                for (auto it = repl.pending.begin(); it != repl.pending.end();) {
                    if (it->second <= ack) {
                        repl.acked.insert(it->first);
                        it = repl.pending.erase(it);
                    } else {
                        {
                            ++it;
                        }
                    }
                }
                for (auto it = repl.removing.begin(); it != repl.removing.end();) {
                    if (it->second <= ack) {
                        repl.base.erase(it->first);
                        repl.acked.erase(it->first);
                        repl.priority.erase(it->first);
                        it = repl.removing.erase(it);
                    } else {
                        {
                            ++it;
                        }
                    }
                }
                for (auto& [nid, comps] : repl.base) {
                    for (auto& [cid, b] : comps) {
                        if (b.tick != 0 && b.tick <= ack) {
                            b.acked = b.sent;
                        }
                    }
                }
            }
            break;
        }
        case Message::Chat: {
            auto chat = serialize::decode<MessageChat>(r);
            if (!r.valid()) {
                break;
            }
            const Peer& p = pe.get<Peer>();
            std::string raw = chat.text.substr(0, 200);
            size_t start = raw.find_first_not_of(' ');
            if (start == std::string::npos) {
                break;
            }
            raw = raw.substr(start, raw.find_last_not_of(' ') - start + 1);
            std::string name = p.username.empty() ? ("player" + std::to_string(p.id)) : p.username;
            if (raw[0] == '/') {
                world.entity().set(RequestCommand{.sender = {.peer = pe, .tank = pe.target<Controls>(), .name = name, .admin = false}, .text = raw});
                break;
            }
            broadcast_chat(world, "<" + name + "> " + raw);
            break;
        }
        case Message::ViewEvent: {
            const Peer& p = pe.get<Peer>();
            std::string name = p.username.empty() ? ("player" + std::to_string(p.id)) : p.username;
            auto ev = serialize::decode<MessageViewEvent>(r);
            if (!r.valid()) {
                break;
            }
            std::vector<std::pair<std::string, std::string>> values;
            for (auto& v : ev.values) {
                values.emplace_back(std::move(v.key), std::move(v.value));
            }
            world.entity().set(RequestViewInteraction{.sender = {.peer = pe, .tank = pe.target<Controls>(), .name = name, .admin = false}, .handler = ev.handler, .values = std::move(values)});
            break;
        }
        default:
            break;
    }
}

void NetworkServer::pump(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto* probe = world.try_get_mut<NetworkHost>();
        if ((probe == nullptr) || (probe->host == nullptr)) {
            return;
        }
        auto& host = *probe;

        host.tick++;
        world.set<ServerClock>({.tick = host.tick, .running = true});

        ENetEvent ev;
        while (enet_host_service(host.host, &ev, 0) > 0) {
            switch (ev.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    handle_packet(world, ev.peer, ev.packet);
                    enet_packet_destroy(ev.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    on_disconnect(world, ev.peer);
                    break;
                default:
                    break;
            }
        }

        world.get<ServerQueries>().inputs.each([&](flecs::entity pe, Peer& p) -> void {
            flecs::entity tank = pe.target<Controls>();
            if (tank && tank.is_alive()) {
                auto hit = p.inputs.find(host.tick);
                if (hit != p.inputs.end()) {
                    uint32_t flags = hit->second.flags;
                    bool shoot = (flags & static_cast<uint32_t>(InputFlags::Shoot)) != 0;
                    const auto* ws = tank.try_get<WeaponStats>();
                    uint64_t cooldown = ws ? ws->cooldown : WeaponStats{}.cooldown;
                    if (shoot && host.tick < p.last_fire + cooldown) {
                        flags &= ~static_cast<uint32_t>(InputFlags::Shoot);
                        shoot = false;
                    }
                    if (shoot) {
                        p.last_fire = host.tick;
                    }
                    tank.set<InputFlags>({flags});
                    tank.set<Firing>({.prediction = hit->second.prediction, .view = hit->second.view, .muzzle = hit->second.muzzle, .aim = hit->second.aim, .aimed = shoot});
                    tank.set<ViewLag>({hit->second.view > VIEW_MAX ? VIEW_MAX : hit->second.view});
                } else {
                    uint32_t last = tank.has<InputFlags>() ? tank.get<InputFlags>().value : 0U;
                    tank.set<InputFlags>({last & ~static_cast<uint32_t>(InputFlags::Shoot)});
                    tank.set<Firing>({.prediction = 0, .view = 0});
                }
            }
            while (!p.inputs.empty() && p.inputs.begin()->first <= host.tick) {
                p.inputs.erase(p.inputs.begin());
            }
            p.simulated = host.tick;
            p.buffer = static_cast<uint32_t>(p.inputs.size());
        });
    }
}

static auto ratify_claim(glm::vec2 bp, glm::vec2 vel, uint32_t lag, uint64_t tick, const History& h, const CollisionBox& box) -> bool {
    float hw = (box.width * 0.5F) + CLAIM_MARGIN;
    float hh = (box.height * 0.5F) + CLAIM_MARGIN;
    for (int i = 0; i < h.count; i++) {
        const TransformSnapshot& s = h.ring[i];
        double bullet_tick = static_cast<double>(s.tick) + static_cast<double>(lag);
        glm::vec2 bpos = bp + vel * static_cast<float>((bullet_tick - static_cast<double>(tick)) * TICK_DT);
        if (math::point_in_box(bpos, s.pos, s.rot, hw, hh)) {
            return true;
        }
    }
    return false;
}

void NetworkServer::hits(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        const auto* host = world.try_get<NetworkHost>();
        if (host == nullptr) {
            return;
        }
        uint64_t tick = host->tick;
        const auto& q = world.get<ServerQueries>();

        q.record.each([&](const Position& p, const Rotation& r, History& h) -> void { h.record(tick, p.value, r.angle); });

        std::vector<flecs::entity> dead_bullets;
        std::vector<flecs::entity> dead_tanks;
        struct OwnedBullet {
            flecs::entity e;
            glm::vec2 pos;
            glm::vec2 vel;
            uint32_t lag;
        };
        std::unordered_map<uint64_t, OwnedBullet> remote;

        q.bullets.each([&](flecs::entity bullet, const Position& bp, const Owner& o) -> void {
            flecs::entity firer = bullet.parent();
            uint32_t lag = 0;
            if (const auto* vl = bullet.try_get<ViewLag>()) {
                lag = vl->ticks;
            } else if (firer && firer.is_alive()) {
                if (const auto* vl = firer.try_get<ViewLag>()) {
                    lag = vl->ticks;
                }
            }

            if (o.peer >= 1) {
                glm::vec2 vel{0};
                if (const auto* v = bullet.try_get<VelocityLinear>()) {
                    vel = v->value;
                }
                remote[(static_cast<uint64_t>(o.peer) << 32) | o.prediction] = {.e = bullet, .pos = bp.value, .vel = vel, .lag = lag};
                return;
            }

            uint64_t view = tick > lag ? tick - lag : 0;
            auto in_obb = [&](glm::vec2 p, const TransformSnapshot* s, const CollisionBox& box) -> bool {
                if (!s) {
                    return false;
                }
                glm::vec2 d = p - s->pos;
                if (math::length_squared(d) > 60.0F * 60.0F) {
                    return false;
                }
                return math::point_in_box(p, s->pos, s->rot, (box.width * 0.5F) + HIT_MARGIN_SERVER, (box.height * 0.5F) + HIT_MARGIN_SERVER);
            };
            const Position* fpos = (firer && firer.is_alive()) ? firer.try_get<Position>() : nullptr;
            bool pointblank = false;
            if (fpos) {
                glm::vec2 fd = bp.value - fpos->value;
                pointblank = math::length_squared(fd) < 80.0F * 80.0F;
            }

            flecs::entity victim;
            float vd2 = 1e30F;
            q.tanks.each([&](flecs::entity tank, const History& h, const CollisionBox& box) -> void {
                if (tank == firer) {
                    return;
                }
                const TransformSnapshot* s = h.at(view);
                if (!s) {
                    return;
                }
                bool hit = in_obb(bp.value, s, box);
                if (!hit && pointblank && fpos) {
                    for (int k = 0; k <= 4 && !hit; ++k) {
                        hit = in_obb(glm::mix(fpos->value, bp.value, static_cast<float>(k) / 4.0F), s, box);
                    }
                }
                if (hit) {
                    glm::vec2 d = bp.value - s->pos;
                    float dd = math::length_squared(d);
                    if (dd < vd2) {
                        vd2 = dd;
                        victim = tank;
                    }
                }
            });
            if (victim) {
                dead_bullets.push_back(bullet);
                dead_tanks.push_back(victim);
            }
        });

        std::unordered_map<uint64_t, flecs::entity> tank_by_id;
        q.tanks_by_id.each([&](flecs::entity e, const NetworkId& id, const History&, const CollisionBox&) -> void { tank_by_id[id.value] = e; });
        q.peers.each([&](flecs::entity, Peer& p, Interest&, Replication&) -> void {
            for (const auto& [prediction, nid] : p.claims) {
                auto bit = remote.find((static_cast<uint64_t>(p.id) << 32) | prediction);
                if (bit == remote.end()) {
                    continue;
                }
                auto tit = tank_by_id.find(nid);
                if (tit == tank_by_id.end()) {
                    continue;
                }
                flecs::entity tank = tit->second;
                if (tank == bit->second.e.parent()) {
                    continue;
                }
                const auto* h = tank.try_get<History>();
                const auto* box = tank.try_get<CollisionBox>();
                if (h && box && ratify_claim(bit->second.pos, bit->second.vel, bit->second.lag, tick, *h, *box)) {
                    dead_bullets.push_back(bit->second.e);
                    dead_tanks.push_back(tank);
                }
            }
            p.claims.clear();
        });

        for (auto e : dead_bullets) {
            if (e.is_alive()) {
                e.destruct();
            }
        }
        for (auto e : dead_tanks) {
            if (!e.is_alive()) {
                continue;
            }
            if (const auto* o = e.try_get<Owner>()) {
                uint32_t pid = o->peer;
                e.set(Position{.value = {300.0F + (static_cast<float>(pid % 8) * 70.0F), 300.0F}}).set(Rotation{.angle = 0}).set(VelocityLinear{}).set(VelocityAngular{}).add<Teleport>();
            } else {
                e.destruct();
            }
        }
    }
}

void NetworkServer::replicate(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        if (world.try_get<NetworkHost>() == nullptr) {
            return;
        }
        auto& host = world.get_mut<NetworkHost>();
        const auto& reg = world.get<NetworkRegistry>();
        const auto& q = world.get<ServerQueries>();

        std::vector<flecs::entity> fresh;
        q.unassigned.run([&](flecs::iter& i) -> void {
            while (i.next()) {
                for (auto k : i) {
                    fresh.push_back(i.entity(k));
                }
            }
        });
        for (auto e : fresh) {
            e.set<NetworkId>({host.next_id++});
        }

        constexpr float CELL = 512.0F;
        auto cell = [](int cx, int cy) -> int64_t { return (static_cast<int64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cy); };
        struct Entry {
            uint64_t nid;
            flecs::entity_t e;
            glm::vec2 pos;
            uint32_t bulletOwner;
        };
        std::unordered_map<int64_t, std::vector<Entry>> grid;

        q.replicated.each([&](flecs::entity e, const NetworkId& nid) -> void {
            glm::vec2 pos{0};
            if (const auto* p = e.try_get<Position>()) {
                pos = p->value;
            }
            uint32_t bulletOwner = 0;
            if (e.has<Bullet>()) {
                if (const auto* o = e.try_get<Owner>()) {
                    bulletOwner = o->peer + 1;
                }
            }
            grid[cell(static_cast<int>(std::floor(pos.x / CELL)), static_cast<int>(std::floor(pos.y / CELL)))].push_back(
                {.nid = nid.value, .e = e.id(), .pos = pos, .bulletOwner = bulletOwner});
        });

        q.peers.each([&](flecs::entity pe, Peer& peer, Interest& interest, Replication& repl) -> void {
            if (!peer.welcomed) {
                return;
            }

            glm::vec2 center{0};
            flecs::entity owned = pe.target<Controls>();
            if (owned && owned.is_alive()) {
                if (const auto* p = owned.try_get<Position>()) {
                    center = p->value;
                }
            }

            float keep = interest.radius * 1.25F;
            int reach = static_cast<int>(std::ceil(keep / CELL));
            int pcx = static_cast<int>(std::floor(center.x / CELL));
            int pcy = static_cast<int>(std::floor(center.y / CELL));
            float r2 = interest.radius * interest.radius;
            float keep2 = keep * keep;

            std::unordered_map<uint64_t, flecs::entity_t> relevant;
            for (int cy = pcy - reach; cy <= pcy + reach; ++cy) {
                for (int cx = pcx - reach; cx <= pcx + reach; ++cx) {
                    auto cit = grid.find(cell(cx, cy));
                    if (cit == grid.end()) {
                        continue;
                    }
                    for (const auto& en : cit->second) {
                        if (en.bulletOwner == peer.id + 1) {
                            continue;
                        }
                        glm::vec2 d = en.pos - center;
                        float d2 = math::length_squared(d);
                        bool tracked = repl.acked.contains(en.nid) || repl.pending.contains(en.nid);
                        if (d2 <= r2 || (tracked && d2 <= keep2)) {
                            relevant[en.nid] = en.e;
                        }
                    }
                }
            }

            send_snapshot(world, reg, host, center, peer, repl, relevant);
        });

        enet_host_flush(host.host);
    }
}
