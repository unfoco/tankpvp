#include "snapshot.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "component/object.h"

#include "protocol.h"

static int collect(flecs::world& world, const NetworkRegistry& reg, NetworkHost& host,
                   Replication& repl, flecs::entity e, uint64_t nid, bool full,
                   std::vector<MessageComponentData>& out) {
    auto& comps = repl.base[nid];
    int n = 0;
    for (const auto& c : reg.components) {
        if (!ecs_has_id(world.c_ptr(), e.id(), c.entity)) continue;
        Writer tmp;
        reg.write(world, e, c, tmp);
        auto& b = comps[c.id];
        if (!full && b.acked == tmp.data) continue;
        out.push_back(MessageComponentData{c.id, tmp.data});
        b.sent = std::move(tmp.data);
        b.tick = host.tick;
        ++n;
    }
    return n;
}

static size_t entity_size(const MessageEntity& em) {
    size_t s = 9;
    for (const auto& cb : em.components) s += 4 + cb.bytes.size();
    return s;
}

void send_snapshot(flecs::world& world, const NetworkRegistry& reg, NetworkHost& host,
                   glm::vec2 view, Peer& peer, Replication& repl,
                   const std::unordered_map<uint64_t, flecs::entity_t>& relevant) {
    auto dist2 = [&](flecs::entity_t eid) {
        glm::vec2 p{0};
        if (const Position* pp = world.entity(eid).try_get<Position>()) p = pp->value;
        glm::vec2 d = p - view;
        return d.x * d.x + d.y * d.y;
    };

    for (const auto& [nid, eid] : relevant) repl.removing.erase(nid);

    for (const auto& [nid, eid] : relevant) {
        float closeness = 1.0f / (1.0f + dist2(eid) / (256.0f * 256.0f));
        float gain = 1.0f + closeness * 4.0f;
        if (!repl.acked.count(nid)) gain += 20.0f;
        float& pr = repl.priority[nid];
        pr = std::min(pr + gain, 10000.0f);
    }
    for (auto it = repl.priority.begin(); it != repl.priority.end();)
        if (!relevant.count(it->first)) it = repl.priority.erase(it); else ++it;

    std::vector<std::pair<float, uint64_t>> order;
    order.reserve(relevant.size());
    for (const auto& [nid, eid] : relevant) order.emplace_back(repl.priority[nid], nid);
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });

    MessageSnapshot   snap;       snap.tick = host.tick; snap.acknowledged_tick = peer.simulated;
                              snap.input_buffer = peer.buffer; snap.send_time = peer.stamp;
    MessageStructural structural; structural.tick = host.tick;
    constexpr size_t BUDGET = 1100;
    size_t used = 0;
    for (const auto& [prio, nid] : order) {
        if (used >= BUDGET) break;
        flecs::entity e = world.entity(relevant.at(nid));
        bool confirmed = repl.acked.count(nid) != 0;

        MessageEntity em; em.network_id = nid;
        int n = collect(world, reg, host, repl, e, nid, !confirmed, em.components);
        size_t esize = entity_size(em);

        if (!confirmed) {
            used += esize;
            structural.spawns.push_back(std::move(em));
            repl.acked.insert(nid);
            repl.pending.erase(nid);
            for (auto& [cid, b] : repl.base[nid]) b.acked = b.sent;
            repl.priority[nid] = 0;
        } else if (n > 0) {
            used += esize;
            snap.deltas.push_back(std::move(em));
            repl.priority[nid] = 0;
        }
    }

    constexpr uint64_t UNSENT = UINT64_MAX;
    for (uint64_t nid : repl.acked)
        if (!relevant.count(nid) && !repl.removing.count(nid)) repl.removing[nid] = UNSENT;
    for (const auto& [nid, tick] : repl.pending)
        if (!relevant.count(nid) && !repl.removing.count(nid)) repl.removing[nid] = UNSENT;

    size_t room = used < BUDGET ? (BUDGET - used) / 8 : 0;
    if (room < 1) room = 1;
    for (auto& [nid, tick] : repl.removing) {
        if (structural.despawns.size() >= room) break;
        structural.despawns.push_back(nid);
    }

    Writer w = wire::message(Message::Snapshot);
    util::encode(w, snap);
    wire::send(peer.peer, w, CHANNEL_UNRELIABLE, false);

    if (!structural.spawns.empty() || !structural.despawns.empty()) {
        Writer s = wire::message(Message::Structural);
        util::encode(s, structural);
        wire::send(peer.peer, s, CHANNEL_RELIABLE, true);

        for (uint64_t nid : structural.despawns) {
            repl.base.erase(nid);
            repl.acked.erase(nid);
            repl.priority.erase(nid);
            repl.removing.erase(nid);
        }
    }
}
