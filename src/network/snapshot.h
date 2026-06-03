#pragma once

#include <flecs.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>

#include "server.h"
#include "registry.h"

void send_snapshot(flecs::world&, const NetworkRegistry&, NetworkHost&, glm::vec2 view,
                   Peer&, Replication&, const std::unordered_map<uint64_t, flecs::entity_t>& relevant);
