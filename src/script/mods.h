#pragma once

#include <string>

#include "state.h"

struct Mods {
    static void load(flecs::world world);
    static auto require(flecs::world world, const std::string& path) -> LuaRef;
};
