#pragma once

#include <flecs.h>

#include "component/object.h"
#include "component/physics.h"
#include "component/world.h"

struct World {
    World(flecs::world& world);

   private:
    static void define(flecs::entity e, const RequestDefineTile& req);
    static void set(flecs::entity e, const RequestSetTile& req);
    static void tileset(flecs::entity e, const RequestLoadTileset& req);
    static void load(flecs::entity e, const RequestLoadChunk& req);
    static void unload(flecs::entity e, const RequestUnloadChunk& req);
    static void damage(flecs::entity e, const RequestDamageTile& req);
    static void sync(flecs::iter& it);
    static void updates(flecs::iter& it);
    static void resolve(flecs::iter& it);
    static void mesh(flecs::iter& it, size_t i, const TileChunk& chunk);
    static void drag(flecs::iter& it, size_t i, const Position& pos, VelocityLinear& vel);
    static void collide(flecs::iter& it);
};
