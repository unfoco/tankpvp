#pragma once

#include <flecs.h>

#include "component/input.h"
#include "component/object.h"

struct Logic {
    Logic(flecs::world&);

private:

    static void input(flecs::iter&, size_t, const InputFlags&, const Position&, Velocity&, Rotation&);
    static void velocity(flecs::iter&, size_t, const Velocity&, Position&);
    static void decay(flecs::entity e, Decay& decay);
};
