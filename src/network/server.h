#pragma once

#include <flecs.h>
#include <enet/enet.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <vector>
#include <unordered_map>
#include <unordered_set>

struct NetworkHost {
    ENetHost* host      = nullptr;
    uint64_t  tick      = 0;
    uint64_t  next_id   = 1;
    uint32_t  next_peer = 1;
    uint16_t  tickrate  = 60;
};

struct Controls {};

struct PendingInput { uint64_t tick = 0; uint32_t flags = 0; uint32_t prediction = 0; uint32_t view = 0; glm::vec2 muzzle{}; float aim = 0; };

struct Peer {
    ENetPeer* peer      = nullptr;
    uint32_t  id        = 0;
    uint64_t  simulated = 0;
    uint32_t  buffer    = 0;
    double    stamp     = 0;
    bool      welcomed  = false;
    uint64_t  last_fire = 0;
    std::map<uint64_t, PendingInput> inputs;

    std::unordered_map<uint32_t, uint64_t> claims;
};

struct Interest { float radius = 1200.0f; };

struct Replication {
    std::unordered_set<uint64_t> acked;
    std::unordered_map<uint64_t, uint64_t> pending;
    std::unordered_map<uint64_t, uint64_t> removing;

    struct Baseline { std::vector<uint8_t> acked; std::vector<uint8_t> sent; uint64_t tick = 0; };
    std::unordered_map<uint64_t, std::unordered_map<uint16_t, Baseline>> base;

    std::unordered_map<uint64_t, float> priority;
};

struct TransformSnapshot { uint64_t tick = 0; glm::vec2 pos{0}; float rot = 0; };
struct History {
    static constexpr int CAP = 64;
    TransformSnapshot ring[CAP];
    int count = 0, head = 0;

    void record(uint64_t tick, glm::vec2 pos, float rot) {
        ring[head] = {tick, pos, rot};
        head = (head + 1) % CAP;
        if (count < CAP) count++;
    }
    const TransformSnapshot* at(uint64_t tick) const {
        const TransformSnapshot* best = nullptr;
        for (int i = 0; i < count; i++) {
            const TransformSnapshot& s = ring[i];
            if (s.tick <= tick && (!best || s.tick > best->tick)) best = &s;
        }
        if (!best)
            for (int i = 0; i < count; i++)
                if (!best || ring[i].tick < best->tick) best = &ring[i];
        return best;
    }
};

struct NetworkServer {
    NetworkServer(flecs::world&);
    static void teardown(flecs::world&);
private:
    static void pump(flecs::iter&);
    static void hits(flecs::iter&);
    static void replicate(flecs::iter&);
};
