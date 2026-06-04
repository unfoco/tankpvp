#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

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

enum class ConnectionState : std::uint8_t {
    Idle,
    Connecting,
    Connected,
    Disconnected,
};

struct ConnectionStatus {
    ConnectionState state = ConnectionState::Idle;
    std::string reason;
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
struct NetworkRequestChat {
    std::string text;
};

struct ServerStatus {
    enum class State : std::uint8_t { Unknown, Querying, Online, Offline, Incompatible };
    State state = State::Unknown;
    uint16_t players = 0;
    uint16_t max = 0;
    uint16_t ping = 0;
};

struct ServerStatusBoard {
    std::unordered_map<std::string, ServerStatus> byAddress;
};

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
