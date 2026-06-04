#pragma once

#include <enet/enet.h>
#include <flecs.h>

#include <cstdint>
#include <string>
#include <unordered_map>

struct QueryPending {
    std::string key;
    double start = 0;
    double sent = 0;
    uint64_t token = 0;
};

struct NetworkQueryState {
    ENetHost* host = nullptr;
    std::unordered_map<ENetPeer*, QueryPending> pending;
    uint64_t next_token = 1;
    double last_query = 0;
};

struct NetworkQuery {
    NetworkQuery(flecs::world& world);

   private:
    static void pump(flecs::iter& it, size_t i, NetworkQueryState& query);
};
