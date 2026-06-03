#pragma once

#include <flecs.h>

#include "component/input.h"
#include "component/object.h"
#include "component/physics.h"

struct Logic {
    Logic(flecs::world& world);

   private:
    static void input(flecs::iter& it, size_t i, const InputFlags& flags, const Position& pos, const Rotation& rot, VelocityLinear& vel, VelocityAngular& ang);
    static void decay(flecs::entity e, Decay& decay);
};
