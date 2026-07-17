#pragma once

#include <enet/enet.h>
#include <flecs.h>

#include <cstdint>
#include <deque>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct NetworkHost {
    ENetHost* host = nullptr;
    uint64_t tick = 0;
    uint64_t next_id = 1;
    uint32_t next_peer = 1;
    uint16_t tickrate = 60;
};

struct PendingInput {
    uint64_t tick = 0;
    glm::vec2 move{};
    uint16_t buttons = 0;
    uint16_t pressed = 0;
    uint32_t prediction = 0;
    uint32_t view = 0;
    float face = 0;
    double sent = 0;
};

struct Peer {
    ENetPeer* peer = nullptr;
    uint32_t id = 0;
    uint64_t simulated = 0;
    uint64_t consumed = 0;
    uint32_t starved = 0;
    bool primed = false;
    uint32_t buffer = 0;
    double stamp = 0;
    bool welcomed = false;
    uint64_t last_fire = 0;
    std::string username;
    std::map<uint64_t, PendingInput> inputs;

    std::deque<uint64_t> asset_queue;
    uint32_t asset_offset = 0;

    std::unordered_set<int64_t> known_chunks;
    uint32_t grid_wipe = 0;
};

struct Interest {
    float radius = 1200.0F;
};

struct Replication {
    std::unordered_set<uint64_t> acked;
    std::unordered_map<uint64_t, uint64_t> pending;
    std::unordered_map<uint64_t, uint64_t> removing;

    struct Baseline {
        std::vector<uint8_t> acked;
        std::vector<uint8_t> sent;
        uint64_t tick = 0;
    };
    std::unordered_map<uint64_t, std::unordered_map<uint16_t, Baseline>> base;

    std::unordered_map<uint64_t, float> priority;
};

struct TransformSnapshot {
    uint64_t tick = 0;
    glm::vec2 pos{0};
    float rot = 0;
};
struct History {
    static constexpr int CAP = 64;
    TransformSnapshot ring[CAP];
    int count = 0, head = 0;

    void record(uint64_t tick, glm::vec2 pos, float rot) {
        ring[head] = {.tick = tick, .pos = pos, .rot = rot};
        head = (head + 1) % CAP;
        if (count < CAP) {
            count++;
        }
    }
    [[nodiscard]] auto at(uint64_t tick) const -> const TransformSnapshot* {
        const TransformSnapshot* best = nullptr;
        for (int i = 0; i < count; i++) {
            const TransformSnapshot& s = ring[i];
            if (s.tick <= tick && ((best == nullptr) || s.tick > best->tick)) {
                best = &s;
            }
        }
        if (best == nullptr) {
            for (int i = 0; i < count; i++) {
                if ((best == nullptr) || ring[i].tick < best->tick) {
                    best = &ring[i];
                }
            }
        }
        return best;
    }
};

struct NetworkServer {
    NetworkServer(flecs::world& world);
    static void teardown(flecs::world& world);

   private:
    static void pump(flecs::iter& it);
    static void hits(flecs::iter& it);
    static void replicate(flecs::iter& it);
};
