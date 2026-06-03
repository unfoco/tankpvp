#include "decode.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "component/interface.h"
#include "component/input.h"
#include "component/network.h"
#include "component/object.h"
#include "component/physics.h"
#include "logic/movement.h"
#include "network.h"
#include "protocol.h"
#include "registry.h"
#include "util/time.h"

static auto mirror(flecs::world& world, NetworkConnection& conn, uint64_t nid) -> flecs::entity {
    auto it = conn.entities.find(nid);
    if (it != conn.entities.end()) {
        flecs::entity e = world.entity(it->second);
        if (e.is_alive()) {
            return e;
        }
        conn.entities.erase(it);
    }
    flecs::entity e = world.entity().set<NetworkId>({nid});
    if (nid == conn.self) {
        e.add<Local>().add<InputFlags>().set<Interpolation>({});
    } else {
        e.add<Remote>().set<Interpolation>({});
    }
    conn.entities[nid] = e.id();
    return e;
}

static void apply_components(flecs::world& world, const NetworkRegistry& reg, NetworkConnection& conn, flecs::entity e, const std::vector<MessageComponentData>& comps, uint64_t tick) {
    bool got_pos = false;
    bool got_rot = false;
    bool is_bullet = false;
    glm::vec2 pos{0};
    float rot = 0;

    for (const auto& cb : comps) {
        auto rit = conn.remap.find(cb.server_id);
        const NetworkRegistry::Component* c = (rit != conn.remap.end()) ? reg.find(rit->second) : nullptr;
        if ((c == nullptr) || !e.is_alive()) {
            continue;
        }
        Reader cr(cb.bytes.data(), cb.bytes.size());
        if (c->name == "Position") {
            Position t{};
            NetworkRegistry::decode(*c, &t, cr);
            pos = t.value;
            got_pos = true;
        } else if (c->name == "Rotation") {
            Rotation t{};
            NetworkRegistry::decode(*c, &t, cr);
            rot = t.angle;
            got_rot = true;
        } else {
            if (c->name == "Bullet") {
                is_bullet = true;
            }
            NetworkRegistry::read(world, e, *c, cr);
        }
    }

    glm::vec2 bvel = is_bullet ? glm::vec2(std::cos(rot), std::sin(rot)) * BULLET_SPEED : glm::vec2(0);

    if (e.is_alive() && (got_pos || got_rot)) {
        bool has_interp = e.has<Interpolation>();
        bool ready = has_interp && e.get<Interpolation>().ready;
        if (got_pos || ready) {
            glm::vec2 p = got_pos || !has_interp ? pos : e.get<Interpolation>().position;
            float a = got_rot || !has_interp ? rot : e.get<Interpolation>().angle;
            if (!e.has<Position>() && (got_pos || has_interp)) {
                e.set<Position>({p});
            }
            if (!e.has<Rotation>() && (got_rot || has_interp)) {
                e.set<Rotation>({a});
            }
            if (has_interp) {
                e.get_mut<Interpolation>().push(p, a, static_cast<double>(tick));
            }
        }
    }
    if (is_bullet && e.is_alive()) {
        e.set<VelocityLinear>({bvel});
    }
}

static void apply_welcome(flecs::world& world, NetworkConnection& conn, Reader& r) {
    auto msg = util::decode<MessageWelcome>(r);
    if (msg.protocol != NETWORK_PROTOCOL) {
        SDL_Log("network: protocol mismatch (server %u, client %u)", msg.protocol, NETWORK_PROTOCOL);
        return;
    }
    conn.peer_id = msg.peer_id;
    conn.self = msg.controlled_entity;
    uint64_t tick = msg.tick;

    world.get_mut<NetworkRegistry>().adopt(world, msg.components, conn.remap);

    conn.welcomed = true;
    conn.newest = tick;
    conn.newest_time = util::now();
    conn.playback = static_cast<double>(tick);
    conn.simulated = tick;
    conn.client_tick = tick + static_cast<uint64_t>(BUFFER_TARGET);

    Writer w = wire::message(Message::Ack);
    MessageAcknowledge ack{tick};
    util::encode(w, ack);
    wire::send(conn.server, w, CHANNEL_RELIABLE, true);

    SDL_Log("network: welcome peer=%u self=%llu components=%zu", conn.peer_id, static_cast<unsigned long long>(conn.self), world.get<NetworkRegistry>().components.size());
}

static auto peek_is_bullet(const NetworkRegistry& reg, NetworkConnection& conn, const std::vector<MessageComponentData>& comps) -> bool {
    for (const auto& cb : comps) {
        auto rit = conn.remap.find(cb.server_id);
        const NetworkRegistry::Component* c = (rit != conn.remap.end()) ? reg.find(rit->second) : nullptr;
        if ((c != nullptr) && c->name == "Bullet") {
            return true;
        }
    }
    return false;
}

static void apply_snapshot(flecs::world& world, NetworkConnection& conn, Reader& r) {
    const auto& reg = world.get<NetworkRegistry>();
    auto snap = util::decode<MessageSnapshot>(r);
    uint64_t tick = snap.tick;
    uint64_t ack = snap.acknowledged_tick;
    uint32_t buffer = snap.input_buffer;
    double stamp = snap.send_time;
    double t = util::now();

    if (stamp > conn.last_stamp) {
        conn.last_stamp = stamp;
        double rt = t - stamp;
        double cap = conn.rtt > 0 ? (conn.rtt * 2.0) + 0.05 : rt;
        conn.rtt = conn.rtt > 0 ? (conn.rtt * 0.9) + (std::min(rt, cap) * 0.1) : rt;
    }

    if (conn.newest != 0 && tick > conn.newest) {
        double arrived = (t - conn.newest_time) / TICK_DT;
        auto expected = static_cast<double>(tick - conn.newest);
        conn.jitter = (conn.jitter * 0.9) + (std::abs(arrived - expected) * 0.1);
    }
    if (tick >= conn.newest) {
        conn.newest = tick;
        conn.newest_time = t;
    }
    conn.delay = std::clamp(2.0 + (conn.jitter * 2.0) + 1.0, 3.0, 12.0);

    conn.simulated = ack;
    conn.buffer = buffer;
    auto& cmds = conn.commands;
    std::erase_if(cmds, [&](const Command& c) -> bool { return c.tick <= ack; });

    for (const auto& d : snap.deltas) {
        auto it = conn.entities.find(d.network_id);
        if (it != conn.entities.end() && world.entity(it->second).is_alive()) {
            apply_components(world, reg, conn, world.entity(it->second), d.components, tick);
        } else if (it != conn.entities.end()) {
            conn.entities.erase(it);
        }
    }

    Writer w = wire::message(Message::Ack);
    MessageAcknowledge ack_msg{tick};
    util::encode(w, ack_msg);
    wire::send(conn.server, w, CHANNEL_UNRELIABLE, false);
}

static void apply_structural(flecs::world& world, NetworkConnection& conn, Reader& r) {
    const auto& reg = world.get<NetworkRegistry>();
    auto s = util::decode<MessageStructural>(r);
    uint64_t tick = s.tick;

    for (const auto& sp : s.spawns) {
        uint64_t nid = sp.network_id;
        bool fresh = !conn.entities.contains(nid);
        bool newBullet = fresh && peek_is_bullet(reg, conn, sp.components);
        flecs::entity e = mirror(world, conn, nid);
        apply_components(world, reg, conn, e, sp.components, tick);
        if (newBullet && e.is_alive()) {
            e.add<Latent>();
        }
        auto& dq = conn.despawn_queue;
        std::erase_if(dq, [&](const auto& d) -> auto { return d.first == nid; });
    }

    for (uint64_t nid : s.despawns) {
        auto eit = conn.entities.find(nid);
        if (eit != conn.entities.end()) {
            conn.despawn_queue.emplace_back(nid, tick);
            flecs::entity e = world.entity(eit->second);
            if (e.is_alive() && e.has<Dying>()) {
                e.set<Dying>({UINT64_MAX});
            }
        }
    }
}

void apply_packet(flecs::world& world, NetworkConnection& conn, ENetPacket* packet) {
    Reader r(packet->data, packet->dataLength);
    switch (static_cast<Message>(r.get<uint8_t>())) {
        case Message::Welcome:
            apply_welcome(world, conn, r);
            break;
        case Message::Snapshot:
            if (conn.welcomed) {
                apply_snapshot(world, conn, r);
            }
            break;
        case Message::Structural:
            if (conn.welcomed) {
                apply_structural(world, conn, r);
            }
            break;
        case Message::Chat:
            if (ChatLog* log = world.try_get_mut<ChatLog>()) {
                log->push(util::decode<MessageChat>(r).text);
            }
            break;
        default:
            break;
    }
}
