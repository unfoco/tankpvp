#include "server.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstring>
#include <vector>
#include <unordered_map>

#include "component/object.h"
#include "component/network.h"
#include "component/physics.h"
#include "component/input.h"

#include "logic/movement.h"

#include "network.h"
#include "protocol.h"
#include "registry.h"
#include "snapshot.h"

static flecs::entity peer_entity(flecs::world& world, ENetPeer* peer) {
    return world.entity(static_cast<flecs::entity_t>(reinterpret_cast<uintptr_t>(peer->data)));
}

struct ServerQueries {
    flecs::query<Peer>                                       inputs;
    flecs::query<const Position, const Rotation, History>    record;
    flecs::query<const Position, const Owner>               bullets;
    flecs::query<const History, const CollisionBox>          tanks;
    flecs::query<const NetworkId, const History, const CollisionBox> tanks_by_id;
    flecs::query<>                                           unassigned;
    flecs::query<const NetworkId>                            replicated;
    flecs::query<Peer, Interest, Replication>               peers;
};

NetworkServer::NetworkServer(flecs::world& world) {
    world.system("network::server::pump").kind(flecs::OnLoad).immediate().run(NetworkServer::pump);
    flecs::entity post_physics = world.lookup("physics::Post");
    flecs::entity hits_phase = world.entity("network::Hits").add(flecs::Phase)
        .depends_on(post_physics ? post_physics : world.entity(flecs::PostUpdate));
    world.system("network::server::hits").kind(hits_phase).immediate().run(NetworkServer::hits);
    world.system("network::server::replicate").kind(flecs::OnStore).immediate().run(NetworkServer::replicate);

    world.set<ServerQueries>({
        .inputs     = world.query_builder<Peer>().build(),
        .record     = world.query_builder<const Position, const Rotation, History>().with<Tank>().build(),
        .bullets    = world.query_builder<const Position, const Owner>().with<Bullet>().build(),
        .tanks      = world.query_builder<const History, const CollisionBox>().with<Tank>().build(),
        .tanks_by_id = world.query_builder<const NetworkId, const History, const CollisionBox>().with<Tank>().build(),
        .unassigned = world.query_builder().with<Replicated>().without<NetworkId>().build(),
        .replicated = world.query_builder<const NetworkId>().with<Replicated>().build(),
        .peers      = world.query_builder<Peer, Interest, Replication>().build(),
    });
}

void NetworkServer::teardown(flecs::world& world) {
    world.defer([&] {
        world.query_builder().with<Peer>().or_().with<Replicated>().build()
            .each([](flecs::entity e) { e.destruct(); });
    });
}

static void send_welcome(flecs::world& world, NetworkHost& host, ENetPeer* peer, uint32_t pid, uint64_t entity) {
    MessageWelcome msg;
    msg.protocol = NETWORK_PROTOCOL;
    msg.peer_id  = pid;
    msg.controlled_entity   = entity;
    msg.tick     = host.tick;
    msg.tickrate = host.tickrate;
    msg.components = world.get<NetworkRegistry>().describe();

    Writer w = wire::message(Message::Welcome);
    util::encode(w, msg);
    wire::send(peer, w, CHANNEL_RELIABLE, true);
}

static void on_connect(flecs::world& world, NetworkHost& host, ENetPeer* epeer) {
    uint32_t pid = host.next_peer++;

    flecs::entity tank = world.entity()
        .set(Color{.value = {50 + (pid * 53) % 205, 50 + (pid * 97) % 205, 50 + (pid * 151) % 205}})
        .set(Position{.value = {300.0f + (pid % 8) * 70.0f, 300.0f}})
        .set(Rotation{.angle = 0})
        .set(VelocityLinear{})
        .set(VelocityAngular{})
        .set(CollisionBox{.height = 30, .width = 40})
        .add<InputFlags>()
        .add<Dynamic>()
        .add<Tank>()
        .set(Firing{})
        .add<History>()
        .add<ViewLag>()
        .set(Owner{.peer = pid});
    uint64_t nid = host.next_id++;
    tank.set<NetworkId>({nid}).add<Replicated>();

    flecs::entity pe = world.entity()
        .set<Peer>({.peer = epeer, .id = pid})
        .add<Interest>()
        .add<Replication>();
    pe.add<Controls>(tank);

    epeer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(pe.id()));
    send_welcome(world, host, epeer, pid, nid);
    SDL_Log("network: peer %u connected (entity %llu)", pid, (unsigned long long)nid);
}

static void on_disconnect(flecs::world& world, ENetPeer* epeer) {
    flecs::entity pe = peer_entity(world, epeer);
    if (pe && pe.is_alive()) {
        flecs::entity tank = pe.target<Controls>();
        if (tank && tank.is_alive()) tank.destruct();
        SDL_Log("network: peer %u disconnected", pe.has<Peer>() ? pe.get<Peer>().id : 0);
        pe.destruct();
    }
    epeer->data = nullptr;
}

static void handle_packet(flecs::world& world, ENetPeer* epeer, ENetPacket* packet) {
    Reader r(packet->data, packet->dataLength);
    Message kind = static_cast<Message>(r.get<uint8_t>());
    flecs::entity pe = peer_entity(world, epeer);
    if (!pe || !pe.is_alive() || !pe.has<Peer>()) return;

    switch (kind) {
    case Message::Input: {
        Peer& p = pe.get_mut<Peer>();
        MessageInput in = util::decode<MessageInput>(r);
        if (in.send_time > p.stamp) p.stamp = in.send_time;
        for (const auto& c : in.commands) {
            if (c.tick_delta > in.newest_tick) continue;
            uint64_t tick = in.newest_tick - c.tick_delta;
            if (tick <= p.simulated || p.inputs.count(tick)) continue;
            p.inputs[tick] = {tick, c.flags, c.prediction, c.view, {c.muzzle_x, c.muzzle_y}, c.aim};
        }
        while (p.inputs.size() > 128) p.inputs.erase(p.inputs.begin());
        break;
    }
    case Message::Hit: {
        Peer& p = pe.get_mut<Peer>();
        MessageHit hit = util::decode<MessageHit>(r);
        for (const auto& c : hit.claims) p.claims[c.prediction] = c.target;
        break;
    }
    case Message::Ack: {
        uint64_t ack = util::decode<MessageAcknowledge>(r).tick;
        Peer& p = pe.get_mut<Peer>();
        if (!p.welcomed) p.welcomed = true;
        if (pe.has<Replication>()) {
            auto& repl = pe.get_mut<Replication>();
            for (auto it = repl.pending.begin(); it != repl.pending.end();)
                if (it->second <= ack) { repl.acked.insert(it->first); it = repl.pending.erase(it); }
                else ++it;
            for (auto it = repl.removing.begin(); it != repl.removing.end();)
                if (it->second <= ack) { repl.base.erase(it->first); repl.acked.erase(it->first);
                                         repl.priority.erase(it->first); it = repl.removing.erase(it); }
                else ++it;
            for (auto& [nid, comps] : repl.base)
                for (auto& [cid, b] : comps)
                    if (b.tick != 0 && b.tick <= ack) b.acked = b.sent;
        }
        break;
    }
    default: break;
    }
}

void NetworkServer::pump(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto* probe = world.try_get_mut<NetworkHost>();
        if (!probe || !probe->host) return;
        auto& host = *probe;

        host.tick++;

        ENetEvent ev;
        while (enet_host_service(host.host, &ev, 0) > 0) {
            switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:    on_connect(world, host, ev.peer); break;
            case ENET_EVENT_TYPE_RECEIVE:    handle_packet(world, ev.peer, ev.packet);
                                             enet_packet_destroy(ev.packet); break;
            case ENET_EVENT_TYPE_DISCONNECT: on_disconnect(world, ev.peer); break;
            default: break;
            }
        }

        world.get<ServerQueries>().inputs.each([&](flecs::entity pe, Peer& p) {
            flecs::entity tank = pe.target<Controls>();
            if (tank && tank.is_alive()) {
                auto hit = p.inputs.find(host.tick);
                if (hit != p.inputs.end()) {
                    uint32_t flags = hit->second.flags;
                    bool shoot = (flags & static_cast<uint32_t>(InputFlags::Shoot)) != 0;
                    if (shoot && host.tick < p.last_fire + FIRE_COOLDOWN) { flags &= ~static_cast<uint32_t>(InputFlags::Shoot); shoot = false; }
                    if (shoot) p.last_fire = host.tick;
                    tank.set<InputFlags>({flags});
                    tank.set<Firing>({hit->second.prediction, hit->second.view,
                                      hit->second.muzzle, hit->second.aim, shoot});
                    tank.set<ViewLag>({hit->second.view > VIEW_MAX ? VIEW_MAX : hit->second.view});
                } else {
                    uint32_t last = tank.has<InputFlags>() ? tank.get<InputFlags>().value : 0u;
                    tank.set<InputFlags>({last & ~static_cast<uint32_t>(InputFlags::Shoot)});
                    tank.set<Firing>({0, 0});
                }
            }
            while (!p.inputs.empty() && p.inputs.begin()->first <= host.tick) p.inputs.erase(p.inputs.begin());
            p.simulated = host.tick;
            p.buffer    = static_cast<uint32_t>(p.inputs.size());
        });
    }
}

static bool ratify_claim(glm::vec2 bp, glm::vec2 vel, uint32_t lag, uint64_t tick,
                         const History& h, const CollisionBox& box) {
    float hw = box.width * 0.5f + CLAIM_MARGIN, hh = box.height * 0.5f + CLAIM_MARGIN;
    for (int i = 0; i < h.count; i++) {
        const TransformSnapshot& s = h.ring[i];
        double bullet_tick = static_cast<double>(s.tick) + static_cast<double>(lag);
        glm::vec2 bpos = bp + vel * static_cast<float>((bullet_tick - static_cast<double>(tick)) * TICK_DT);
        if (point_in_obb(bpos, s.pos, s.rot, hw, hh)) return true;
    }
    return false;
}

void NetworkServer::hits(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        const NetworkHost* host = world.try_get<NetworkHost>();
        if (!host) return;
        uint64_t tick = host->tick;
        const ServerQueries& q = world.get<ServerQueries>();

        q.record.each([&](const Position& p, const Rotation& r, History& h) {
            h.record(tick, p.value, r.angle);
        });

        std::vector<flecs::entity> dead_bullets, dead_tanks;
        struct OwnedBullet { flecs::entity e; glm::vec2 pos; glm::vec2 vel; uint32_t lag; };
        std::unordered_map<uint64_t, OwnedBullet> remote;

        q.bullets.each([&](flecs::entity bullet, const Position& bp, const Owner& o) {
            flecs::entity firer = bullet.parent();
            uint32_t lag = 0;
            if (const ViewLag* vl = bullet.try_get<ViewLag>()) lag = vl->ticks;
            else if (firer && firer.is_alive())
                if (const ViewLag* vl = firer.try_get<ViewLag>()) lag = vl->ticks;

            if (o.peer >= 1) {
                glm::vec2 vel{BULLET_SPEED, 0};
                if (const VelocityLinear* v = bullet.try_get<VelocityLinear>()) vel = v->value;
                remote[(static_cast<uint64_t>(o.peer) << 32) | o.prediction] = {bullet, bp.value, vel, lag};
                return;
            }

            uint64_t view = tick > lag ? tick - lag : 0;
            auto in_obb = [&](glm::vec2 p, const TransformSnapshot* s, const CollisionBox& box) {
                if (!s) return false;
                glm::vec2 d = p - s->pos;
                if (d.x * d.x + d.y * d.y > 60.0f * 60.0f) return false;
                return point_in_obb(p, s->pos, s->rot,
                                    box.width * 0.5f + HIT_MARGIN_SERVER, box.height * 0.5f + HIT_MARGIN_SERVER);
            };
            const Position* fpos = (firer && firer.is_alive()) ? firer.try_get<Position>() : nullptr;
            bool pointblank = false;
            if (fpos) { glm::vec2 fd = bp.value - fpos->value; pointblank = fd.x * fd.x + fd.y * fd.y < 80.0f * 80.0f; }

            flecs::entity victim; float vd2 = 1e30f;
            q.tanks.each([&](flecs::entity tank, const History& h, const CollisionBox& box) {
                if (tank == firer) return;
                const TransformSnapshot* s = h.at(view);
                if (!s) return;
                bool hit = in_obb(bp.value, s, box);
                if (!hit && pointblank && fpos)
                    for (int k = 0; k <= 4 && !hit; ++k)
                        hit = in_obb(glm::mix(fpos->value, bp.value, k / 4.0f), s, box);
                if (hit) {
                    glm::vec2 d = bp.value - s->pos;
                    float dd = d.x * d.x + d.y * d.y;
                    if (dd < vd2) { vd2 = dd; victim = tank; }
                }
            });
            if (victim) { dead_bullets.push_back(bullet); dead_tanks.push_back(victim); }
        });

        std::unordered_map<uint64_t, flecs::entity> tank_by_id;
        q.tanks_by_id.each([&](flecs::entity e, const NetworkId& id, const History&, const CollisionBox&) {
            tank_by_id[id.value] = e;
        });
        q.peers.each([&](flecs::entity, Peer& p, Interest&, Replication&) {
            for (const auto& [prediction, nid] : p.claims) {
                auto bit = remote.find((static_cast<uint64_t>(p.id) << 32) | prediction);
                if (bit == remote.end()) continue;
                auto tit = tank_by_id.find(nid);
                if (tit == tank_by_id.end()) continue;
                flecs::entity tank = tit->second;
                if (tank == bit->second.e.parent()) continue;
                const History* h = tank.try_get<History>();
                const CollisionBox* box = tank.try_get<CollisionBox>();
                if (h && box && ratify_claim(bit->second.pos, bit->second.vel, bit->second.lag, tick, *h, *box)) {
                    dead_bullets.push_back(bit->second.e);
                    dead_tanks.push_back(tank);
                }
            }
            p.claims.clear();
        });

        for (auto e : dead_bullets) if (e.is_alive()) e.destruct();
        for (auto e : dead_tanks) {
            if (!e.is_alive()) continue;
            if (const Owner* o = e.try_get<Owner>()) {
                uint32_t pid = o->peer;
                e.set(Position{.value = {300.0f + (pid % 8) * 70.0f, 300.0f}})
                 .set(Rotation{.angle = 0})
                 .set(VelocityLinear{})
                 .set(VelocityAngular{})
                 .add<Teleport>();
            } else {
                e.destruct();
            }
        }
    }
}

void NetworkServer::replicate(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        if (!world.try_get<NetworkHost>()) return;
        auto& host = world.get_mut<NetworkHost>();
        const auto& reg = world.get<NetworkRegistry>();
        const ServerQueries& q = world.get<ServerQueries>();

        std::vector<flecs::entity> fresh;
        q.unassigned.run([&](flecs::iter& i) { while (i.next()) for (auto k : i) fresh.push_back(i.entity(k)); });
        for (auto e : fresh) e.set<NetworkId>({host.next_id++});

        constexpr float CELL = 512.0f;
        auto cell = [](int cx, int cy) {
            return (static_cast<int64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cy);
        };
        struct Entry { uint64_t nid; flecs::entity_t e; glm::vec2 pos; uint32_t bulletOwner; };
        std::unordered_map<int64_t, std::vector<Entry>> grid;

        q.replicated.each([&](flecs::entity e, const NetworkId& nid) {
            glm::vec2 pos{0};
            if (const Position* p = e.try_get<Position>()) pos = p->value;
            uint32_t bulletOwner = 0;
            if (e.has<Bullet>()) if (const Owner* o = e.try_get<Owner>()) bulletOwner = o->peer + 1;
            grid[cell((int)std::floor(pos.x / CELL), (int)std::floor(pos.y / CELL))].push_back({nid.value, e.id(), pos, bulletOwner});
        });

        q.peers.each([&](flecs::entity pe, Peer& peer, Interest& interest, Replication& repl) {
            if (!peer.welcomed) return;

            glm::vec2 center{0};
            flecs::entity owned = pe.target<Controls>();
            if (owned && owned.is_alive()) if (const Position* p = owned.try_get<Position>()) center = p->value;

            float keep = interest.radius * 1.25f;
            int reach = (int)std::ceil(keep / CELL);
            int pcx = (int)std::floor(center.x / CELL), pcy = (int)std::floor(center.y / CELL);
            float r2 = interest.radius * interest.radius, keep2 = keep * keep;

            std::unordered_map<uint64_t, flecs::entity_t> relevant;
            for (int cy = pcy - reach; cy <= pcy + reach; ++cy)
                for (int cx = pcx - reach; cx <= pcx + reach; ++cx) {
                    auto cit = grid.find(cell(cx, cy));
                    if (cit == grid.end()) continue;
                    for (const auto& en : cit->second) {
                        if (en.bulletOwner == peer.id + 1) continue;
                        glm::vec2 d = en.pos - center;
                        float d2 = d.x * d.x + d.y * d.y;
                        bool tracked = repl.acked.count(en.nid) || repl.pending.count(en.nid);
                        if (d2 <= r2 || (tracked && d2 <= keep2)) relevant[en.nid] = en.e;
                    }
                }

            send_snapshot(world, reg, host, center, peer, repl, relevant);
        });

        enet_host_flush(host.host);
    }
}
