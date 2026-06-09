#pragma once

#include <enet/enet.h>
#include <flecs.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

#include "component/network.h"

constexpr double BUFFER_TARGET = 4.0;

struct Sample {
    glm::vec2 position{0};
    float angle = 0;
    double tick = 0;
};

struct Interpolation {
    static constexpr int CAP = 16;
    Sample ring[CAP];
    int count = 0, head = 0;
    glm::vec2 position{0};
    float angle = 0;
    uint64_t tick = 0;
    bool ready = false;

    glm::vec2 vis_error{0};
    float vis_error_angle = 0;
    glm::vec2 predicted_prev{0};
    float predicted_prev_angle = 0;
    bool has_predicted_prev = false;

    void push(glm::vec2 p, float a, double t) {
        ring[head] = {.position = p, .angle = a, .tick = t};
        head = (head + 1) % CAP;
        if (count < CAP) {
            count++;
        }
        position = p;
        angle = a;
        tick = static_cast<uint64_t>(t);
        ready = true;
    }
    [[nodiscard]] auto at(int i) const -> const Sample& {
        return ring[(head - count + i + (2 * CAP)) % CAP];
    }
};

struct Command {
    uint64_t tick = 0;
    uint32_t flags = 0;
    uint32_t prediction = 0;
    uint32_t view = 0;
    double sent = 0;
    glm::vec2 muzzle{};
    float aim = 0;
};

struct NetworkConnection {
    ENetHost* host = nullptr;
    ENetPeer* server = nullptr;
    bool connected = false;
    uint32_t peer_id = 0;
    uint64_t self = 0;
    bool welcomed = false;

    std::unordered_map<uint64_t, flecs::entity_t> entities;
    std::unordered_map<uint16_t, uint16_t> remap;
    uint16_t registry_version = 0;
    uint32_t tile_version = UINT32_MAX;
    std::vector<uint8_t> deferred_snapshot;

    std::vector<std::pair<uint64_t, uint64_t>> despawn_queue;

    uint64_t newest = 0;
    double newest_time = 0;
    double playback = 0;
    double delay = 4;
    double jitter = 0;

    std::vector<Command> commands;
    uint64_t client_tick = 0;
    uint64_t simulated = 0;
    uint32_t predictions = 0;
    bool fire_pending = false;
    uint64_t last_fire = 0;

    uint32_t buffer = 0;
    double buffer_avg = 2.0;
    double rtt = 0;
    double last_stamp = 0;
};

struct NetworkClient {
    NetworkClient(flecs::world& world);
    static void teardown(flecs::world& world);

   private:
    static void pump(flecs::iter& it);
    static void predict(flecs::iter& it);
    static void interpolate(flecs::iter& it);
    static void upload(flecs::iter& it);
    static void chat(flecs::entity e, const RequestChat& req);
};
