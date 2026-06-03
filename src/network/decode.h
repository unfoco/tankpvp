#pragma once

#include <enet/enet.h>
#include <flecs.h>

#include "client.h"

void apply_packet(flecs::world& world, NetworkConnection& conn, ENetPacket* packet);
