#pragma once

#include <lua.h>
#include <lualib.h>

#include <LuaBridge/LuaBridge.h>

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "component/script.h"

using luabridge::LuaRef;

struct ScriptEntity {
    flecs::entity entity;
};

struct ScriptPlayer {
    flecs::entity peer;
};

struct ScriptSignal {
    int id = 0;
};

struct ScriptProto {
    std::string category;
};

struct ScriptSub {
    int kind = 0;
    std::string name;
    int slot = 0;
};

struct ViewHandler {
    uint64_t owner = 0;
    std::string view;
    LuaRef fn;
};

struct ScriptTimer {
    uint64_t due = 0;
    uint64_t interval = 0;
    int id = 0;
    LuaRef fn;
};

struct WireField {
    std::string name;
    std::string type;
    auto operator==(const WireField&) const -> bool = default;
};

struct ComponentDef {
    std::string name;
    std::vector<WireField> fields;
    bool replicated = true;
    bool is_tag = false;
    auto operator==(const ComponentDef&) const -> bool = default;
};

struct ScriptState {
    lua_State* lua = nullptr;
    uint32_t view_next = 0;
    std::unordered_map<std::string, std::vector<LuaRef>> handlers;
    std::unordered_map<std::string, LuaRef> commands;
    std::unordered_map<std::string, std::vector<std::string>> enum_aliases;
    std::unordered_map<std::string, std::unordered_map<std::string, LuaRef>> prototypes;
    std::map<std::string, std::vector<std::string>> proto_defs;
    std::unordered_map<uint32_t, ViewHandler> view_owner;
    std::unordered_map<std::string, std::vector<CommandArgument>> inferred;
    std::unordered_map<std::string, flecs::entity_t> components;
    std::unordered_set<std::string> author_components;
    std::map<std::string, ComponentDef> component_defs;
    std::unordered_set<std::string> declared_this_load;
    std::unordered_map<std::string, std::vector<LuaRef>> component_handlers;
    std::unordered_map<std::string, std::vector<LuaRef>> component_add_handlers;
    std::unordered_map<std::string, std::vector<LuaRef>> component_remove_handlers;
    std::unordered_set<std::string> observed;
    std::unordered_map<uint64_t, std::string> usernames;
    std::vector<std::string> inferred_events;
    size_t inferred_next = 0;
    std::unordered_map<int, std::vector<LuaRef>> signal_handlers;
    int signal_next = 0;
    std::unordered_map<uint64_t, LuaRef> query_callbacks;
    std::unordered_map<std::string, LuaRef> modules;
    int timer_next = 0;
    bool reload_pending = false;
    std::map<std::string, std::vector<std::string>> api_decls;
    std::map<std::string, std::vector<std::string>> api_types;
    std::vector<ScriptTimer> timers;

    static auto of(flecs::world world) -> ScriptState& { return world.get_mut<ScriptState>(); }

    ScriptState() = default;
    ScriptState(const ScriptState&) = delete;
    auto operator=(const ScriptState&) -> ScriptState& = delete;
    ScriptState(ScriptState&& other) noexcept { swap(other); }
    auto operator=(ScriptState&& other) noexcept -> ScriptState& {
        if (this != &other) {
            reset();
            swap(other);
        }
        return *this;
    }
    ~ScriptState() { reset(); }

    void swap(ScriptState& other) noexcept {
        std::swap(lua, other.lua);
        std::swap(view_next, other.view_next);
        handlers.swap(other.handlers);
        commands.swap(other.commands);
        enum_aliases.swap(other.enum_aliases);
        prototypes.swap(other.prototypes);
        proto_defs.swap(other.proto_defs);
        view_owner.swap(other.view_owner);
        inferred.swap(other.inferred);
        components.swap(other.components);
        author_components.swap(other.author_components);
        component_defs.swap(other.component_defs);
        declared_this_load.swap(other.declared_this_load);
        component_handlers.swap(other.component_handlers);
        component_add_handlers.swap(other.component_add_handlers);
        component_remove_handlers.swap(other.component_remove_handlers);
        observed.swap(other.observed);
        usernames.swap(other.usernames);
        inferred_events.swap(other.inferred_events);
        std::swap(inferred_next, other.inferred_next);
        signal_handlers.swap(other.signal_handlers);
        std::swap(signal_next, other.signal_next);
        query_callbacks.swap(other.query_callbacks);
        modules.swap(other.modules);
        api_decls.swap(other.api_decls);
        api_types.swap(other.api_types);
        timers.swap(other.timers);
    }
    void reset() {
        handlers.clear();
        inferred_events.clear();
        inferred_next = 0;
        component_handlers.clear();
        component_add_handlers.clear();
        component_remove_handlers.clear();
        usernames.clear();
        commands.clear();
        prototypes.clear();
        view_owner.clear();
        timers.clear();
        if (lua != nullptr) {
            lua_close(lua);
            lua = nullptr;
        }
    }
};

struct Lua {
    template <class T>
    static void push(lua_State* state, T&& value) {
        (void)luabridge::push(state, std::forward<T>(value));
    }
    static auto ref_number(const LuaRef& table, const char* key, double fallback) -> double {
        LuaRef value = table[key];
        return value.isNumber() ? value.unsafe_cast<double>() : fallback;
    }
    static auto ref_string(const LuaRef& table, const char* key, const std::string& fallback) -> std::string {
        LuaRef value = table[key];
        return value.isString() ? value.unsafe_cast<std::string>() : fallback;
    }
    static auto ref_bool(const LuaRef& table, const char* key, bool fallback) -> bool {
        LuaRef value = table[key];
        return value.isBool() ? value.unsafe_cast<bool>() : fallback;
    }
};

struct BudgetGuard {
    static constexpr int LIMIT = 5'000'000;
    inline static thread_local int64_t budget = 0;
    lua_State* lua;

    explicit BudgetGuard(lua_State* state) : lua(state) {
        budget = LIMIT;
        lua_callbacks(lua)->interrupt = &interrupt;
    }
    BudgetGuard(const BudgetGuard&) = delete;
    auto operator=(const BudgetGuard&) -> BudgetGuard& = delete;
    BudgetGuard(BudgetGuard&&) = delete;
    auto operator=(BudgetGuard&&) -> BudgetGuard& = delete;
    ~BudgetGuard() { lua_callbacks(lua)->interrupt = nullptr; }

    static void interrupt(lua_State* state, int gc) {
        if (gc >= 0) {
            return;
        }
        if (--budget < 0) {
            budget = INT64_MAX;
            luaL_error(state, "script exceeded instruction budget");
        }
    }
};
