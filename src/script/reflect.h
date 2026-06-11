#pragma once

#include <utility>
#include <vector>

#include "state.h"

struct EngineEnum {
    const char* name;
    std::vector<std::pair<const char*, int>> constants;
};

struct Reflect {
    static auto engine_enums() -> const std::vector<EngineEnum>&;
    static void expose_enums(flecs::world world);

    static auto component_entity(flecs::world world, const std::string& name) -> flecs::entity_t;
    static auto component_ref_name(const LuaRef& ref) -> std::string;
    static auto is_known_component(flecs::world world, const std::string& name) -> bool;
    static auto entity_has_component(flecs::entity entity, const std::string& component) -> bool;
    static auto component_to_ref(lua_State* state, flecs::entity entity, const std::string& name) -> LuaRef;
    static void set_component_from_ref(flecs::entity entity, const std::string& name, const LuaRef& value);
    static void define_component(flecs::world world, const ComponentDef& def);
    static void remove_component(flecs::world world, const std::string& name);
    static void refresh_components(flecs::world world);
    static void register_component(flecs::world world, const std::string& name, flecs::entity_t comp);
};
