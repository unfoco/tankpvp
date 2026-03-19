#pragma once

#include <flecs.h>

#include "component/input.h"
#include "component/object.h"

struct Logic {
    Logic(flecs::world&);

private:

    static void input(flecs::iter&, size_t, const InputFlags&, Position&, Rotation&);
    static void bullet(flecs::iter&, size_t, Position& pos, const Velocity& vel);
    static void decay(flecs::entity e, Decay& decay);
};
