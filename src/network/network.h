#pragma once

#include <flecs.h>

#include "component/network.h"

constexpr float TICK_DT = 1.0f / 60.0f;

struct Network {
    Network(flecs::world&);

    static void host(flecs::entity, const NetworkRequestHost&);
    static void join(flecs::entity, const NetworkRequestJoin&);
    static void quit(flecs::entity, const NetworkRequestQuit&);
};
