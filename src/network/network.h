#pragma once

#include <flecs.h>

#include "component/network.h"

constexpr float TICK_DT = 1.0F / 60.0F;

struct Network {
    Network(flecs::world& world);

    static void host(flecs::entity e, const NetworkRequestHost& req);
    static void join(flecs::entity e, const NetworkRequestJoin& req);
    static void quit(flecs::entity e, const NetworkRequestQuit& req);
};
