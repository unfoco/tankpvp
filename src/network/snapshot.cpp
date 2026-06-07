#include "snapshot.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "component/object.h"
#include "util/math.h"
#include "protocol.h"

static auto collect(flecs::world& world, const NetworkRegistry& reg, NetworkHost& host, Replication& repl, flecs::entity e, uint64_t nid, bool full, std::vector<MessageComponentData>& out)
    -> int {
    auto& comps = repl.base[nid];
    int n = 0;
    for (const auto& c : reg.components) {
        if (!ecs_has_id(world.c_ptr(), e.id(), c.entity)) {
            continue;
        }
        serialize::Writer tmp;
        NetworkRegistry::write(world, e, c, tmp);
        auto& b = comps[c.id];
        if (!full && b.acked == tmp.data) {
            continue;
        }
        out.push_back(MessageComponentData{.server_id = c.id, .bytes = tmp.data});
        b.sent = std::move(tmp.data);
        b.tick = host.tick;
        ++n;
    }
    return n;
}

static auto entity_size(const MessageEntity& em) -> size_t {
    size_t s = 9;
    for (const auto& cb : em.components) {
        s += 4 + cb.bytes.size();
    }
    return s;
}

void send_snapshot(flecs::world& world, const NetworkRegistry& reg, NetworkHost& host, glm::vec2 view, Peer& peer, Replication& repl,
                   const std::unordered_map<uint64_t, flecs::entity_t>& relevant) {
    auto dist2 = [&](flecs::entity_t eid) -> float {
        glm::vec2 p{0};
        if (const auto* pp = world.entity(eid).try_get<Position>()) {
            p = pp->value;
        }
        glm::vec2 d = p - view;
        return math::length_squared(d);
    };

    for (const auto& [nid, eid] : relevant) {
        repl.removing.erase(nid);
    }

    for (const auto& [nid, eid] : relevant) {
        float closeness = 1.0F / (1.0F + (dist2(eid) / (256.0F * 256.0F)));
        float gain = 1.0F + (closeness * 4.0F);
        if (static_cast<unsigned int>(repl.acked.contains(nid)) == 0U) {
            gain += 20.0F;
        }
        float& pr = repl.priority[nid];
        pr = std::min(pr + gain, 10000.0F);
    }
    for (auto it = repl.priority.begin(); it != repl.priority.end();) {
        if (static_cast<unsigned int>(relevant.contains(it->first)) == 0U) {
            it = repl.priority.erase(it);
        } else {
            ++it;
        }
    }

    std::vector<std::pair<float, uint64_t>> order;
    order.reserve(relevant.size());
    for (const auto& [nid, eid] : relevant) {
        order.emplace_back(repl.priority[nid], nid);
    }
    std::ranges::sort(order, [](auto& a, auto& b) -> auto { return a.first > b.first; });

    MessageSnapshot snap;
    snap.tick = host.tick;
    snap.acknowledged_tick = peer.simulated;
    snap.input_buffer = peer.buffer;
    snap.send_time = peer.stamp;
    snap.registry_version = reg.version;
    MessageStructural structural;
    structural.tick = host.tick;
    constexpr size_t BUDGET = 1100;
    size_t used = 0;
    for (const auto& [prio, nid] : order) {
        if (used >= BUDGET) {
            break;
        }
        flecs::entity e = world.entity(relevant.at(nid));
        bool confirmed = repl.acked.contains(nid);

        MessageEntity em;
        em.network_id = nid;
        int n = collect(world, reg, host, repl, e, nid, !confirmed, em.components);
        size_t esize = entity_size(em);

        if (!confirmed) {
            used += esize;
            structural.spawns.push_back(std::move(em));
            repl.acked.insert(nid);
            repl.pending.erase(nid);
            for (auto& [cid, b] : repl.base[nid]) {
                b.acked = b.sent;
            }
            repl.priority[nid] = 0;
        } else if (n > 0) {
            used += esize;
            snap.deltas.push_back(std::move(em));
            repl.priority[nid] = 0;
        }
    }

    constexpr uint64_t UNSENT = UINT64_MAX;
    for (uint64_t nid : repl.acked) {
        if ((static_cast<unsigned int>(relevant.contains(nid)) == 0U) && (static_cast<unsigned int>(repl.removing.contains(nid)) == 0U)) {
            repl.removing[nid] = UNSENT;
        }
    }
    for (const auto& [nid, tick] : repl.pending) {
        if ((static_cast<unsigned int>(relevant.contains(nid)) == 0U) && (static_cast<unsigned int>(repl.removing.contains(nid)) == 0U)) {
            repl.removing[nid] = UNSENT;
        }
    }

    size_t room = used < BUDGET ? (BUDGET - used) / 8 : 0;
    room = std::max<size_t>(room, 1);
    for (auto& [nid, tick] : repl.removing) {
        if (structural.despawns.size() >= room) {
            break;
        }
        structural.despawns.push_back(nid);
    }

    serialize::Writer w = wire::message(Message::Snapshot);
    serialize::encode(w, snap);
    wire::send(peer.peer, w, CHANNEL_UNRELIABLE, false);

    if (!structural.spawns.empty() || !structural.despawns.empty()) {
        serialize::Writer s = wire::message(Message::Structural);
        serialize::encode(s, structural);
        wire::send(peer.peer, s, CHANNEL_RELIABLE, true);

        for (uint64_t nid : structural.despawns) {
            repl.base.erase(nid);
            repl.acked.erase(nid);
            repl.priority.erase(nid);
            repl.removing.erase(nid);
        }
    }
}
