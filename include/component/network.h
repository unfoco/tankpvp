#pragma once

#include <string>

#include <enet/enet.h>

struct NetworkTarget { std::string address; uint16_t port; };
struct NetworkEventHost{ std::string address; uint16_t port; };
struct NetworkEventJoin{ std::string address; uint16_t port; };
struct NetworkEventQuit{};

struct NetworkHost{
    ENetHost* target;
    bool server;
};

struct NetworkConnection{
    ENetPeer* peer;
};
