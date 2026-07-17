#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "state.h"

struct Mods {
    static void load(flecs::world world);
    static auto require(flecs::world world, const std::string& path) -> LuaRef;

   private:
    static auto resolve_order(flecs::world world) -> std::vector<std::filesystem::path>;
};
