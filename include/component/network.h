#pragma once

#include <string>

#include <enet/enet.h>

struct Remote {};
struct Controls {};

struct NetworkId { uint32_t value = 0; };

struct NetworkServer {
    ENetHost* host = nullptr;
};

struct NetworkPeer {
    ENetPeer* peer = nullptr;
};

struct NetworkClient {
    ENetHost* host = nullptr;
    ENetPeer* server = nullptr;
};

struct NetworkRequestHost { std::string address; uint16_t port; };
struct NetworkRequestJoin { std::string address; uint16_t port; };
struct NetworkRequestQuit {};

struct NetworkTarget { std::string address; uint16_t port = 0; };
