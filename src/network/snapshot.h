#pragma once

#include <flecs.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>

#include "registry.h"
#include "server.h"

void send_snapshot(flecs::world& world, const NetworkRegistry& registry, NetworkHost& host, glm::vec2 view, Peer& peer, Replication& replication,
                   const std::unordered_map<uint64_t, flecs::entity_t>& relevant);
