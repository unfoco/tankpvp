#pragma once

#include <flecs.h>

#include "component/input.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/render.h"

struct Logic {
    Logic(flecs::world& world);

   private:
    static void input(flecs::iter& it, size_t i, const InputFlags& flags, const Position& pos, const Rotation& rot, VelocityLinear& vel, VelocityAngular& ang);
    static void reload(flecs::iter& it, size_t i, Ammo& ammo);
    static void decay(flecs::entity e, Decay& decay);
    static void camera_decay(flecs::iter& it, size_t i, Camera& cam);
    static void flash_decay(flecs::iter& it, size_t i, PostStack& post);
};
