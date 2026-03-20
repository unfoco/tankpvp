#pragma once

#include <flecs.h>

#include "component/input.h"
#include "component/object.h"
#include "component/physics.h"

struct Logic {
    Logic(flecs::world&);

private:

    static void input(flecs::iter&, size_t, const InputFlags&, const Position&, const Rotation&, LinearVelocity&, AngularVelocity&);
    static void decay(flecs::entity e, Decay& decay);
};
