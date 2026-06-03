#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

enum class NetworkRole : std::uint8_t {
    Shared,
    Server,
    Client,
};

struct NetworkConfig {
    NetworkRole role = NetworkRole::Shared;
    std::string address = "0.0.0.0";
    uint16_t port = 5000;
    uint16_t tickrate = 60;
};

struct SimulationClock {
    double scale = 1.0;
};

struct NetworkRequestHost {
    std::string address;
    uint16_t port;
};
struct NetworkRequestJoin {
    std::string address;
    uint16_t port;
};
struct NetworkRequestQuit {};

struct NetworkTarget {
    std::string address;
    uint16_t port = 0;
};

struct NetworkId {
    uint64_t value = 0;
};

struct Replicated {};
struct Remote {};

struct Predicted {
    float life = 0;
    uint32_t id = 0;
};

struct Owner {
    uint32_t peer = 0;
    uint32_t prediction = 0;
};

struct Firing {
    uint32_t prediction = 0;
    uint32_t view = 0;
    glm::vec2 muzzle{};
    float aim = 0;
    bool aimed = false;
};

struct Networked {};

struct Quantize {
    float precision = 0;
    uint8_t bytes = 4;
};

struct ViewLag {
    uint32_t ticks = 0;
};

struct Latent {};

struct Dying {
    uint64_t revive_tick = 0;
};
