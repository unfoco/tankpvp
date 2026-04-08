#pragma once

#include <flecs.h>
#include <enet/enet.h>

#include "component/network.h"

// server mode: Logic module + NetworkHost
// client mode: NetworkClient (just replication)
// (server authoritative)

struct Network {
    Network(flecs::world&);

private:

    static void host(flecs::entity, const NetworkRequestHost&);
    static void join(flecs::entity, const NetworkRequestJoin&);
    static void quit(flecs::entity, const NetworkRequestQuit&);
};
