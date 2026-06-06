#pragma once

#include <flecs.h>
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

struct ServerClock {
    uint64_t tick = 0;
    bool running = false;
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

struct NetworkTarget {
    std::string address;
    uint16_t port = 0;
};

struct RequestHost {
    std::string address;
    uint16_t port;
};
struct RequestJoin {
    std::string address;
    uint16_t port;
};
struct RequestQuit {};
struct RequestChat {
    std::string text;
};
struct RequestBroadcast {
    std::string line;
};
struct RequestReply {
    flecs::entity peer;
    std::string line;
};

struct ServerStatus {
    enum class State : std::uint8_t { Unknown, Querying, Online, Offline, Incompatible };
    State state = State::Unknown;
    uint16_t players = 0;
    uint16_t max = 0;
    uint16_t ping = 0;
};

struct ServerStatusBoard {
    std::unordered_map<std::string, ServerStatus> by_address;
};

struct NetworkId {
    uint64_t value = 0;
};

struct Networked {};
struct Replicated {};
struct Local {};
struct Remote {};

struct Quantize {
    float precision = 0;
    uint8_t bytes = 4;
};

struct Owner {
    uint32_t peer = 0;
    uint32_t prediction = 0;
};

struct Controls {};

struct Predicted {
    float life = 0;
    uint32_t id = 0;
};

struct Firing {
    uint32_t prediction = 0;
    uint32_t view = 0;
    glm::vec2 muzzle{};
    float aim = 0;
    bool aimed = false;
};

struct ViewLag {
    uint32_t ticks = 0;
};

struct Latent {};

struct Dying {
    uint64_t revive = 0;
};
