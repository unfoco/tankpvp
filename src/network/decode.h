#pragma once

#include <flecs.h>
#include <enet/enet.h>

#include "client.h"

void apply_packet(flecs::world&, NetworkConnection&, ENetPacket*);
