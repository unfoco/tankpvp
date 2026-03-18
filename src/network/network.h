#pragma once

#include <flecs.h>

// server mode: Logic module + NetworkHost
// client mode: NetworkClient (just replication)
// (server authoritative)

struct Network {
    Network(flecs::world&);

private:
};
