#pragma once

#include <flecs.h>

#include "component/input.h"
#include "component/network.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/render.h"

inline auto is_client(flecs::world world) -> bool {
    const auto* cfg = world.try_get<NetworkConfig>();
    return cfg != nullptr && cfg->role == NetworkRole::Client;
}

struct Sim {
    Sim(flecs::world& world);

   private:
    static void input(flecs::iter& it, size_t i, const InputState& in, const Position& pos, const Rotation& rot, VelocityLinear& vel, VelocityAngular& ang);
    static void reload(flecs::iter& it, size_t i, Ammo& ammo);
    static void decay(flecs::entity e, Lifetime& decay);
    static void camera_decay(flecs::iter& it, size_t i, Camera& cam);
    static void flash_decay(flecs::iter& it, size_t i, PostStack& post);
};
