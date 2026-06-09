#include "script.h"

#include <SDL3/SDL.h>
#include <lua.h>
#include <lualib.h>

#include <LuaBridge/LuaBridge.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "component/asset.h"
#include "component/world.h"
#include "component/audio.h"
#include "component/network.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/script.h"

#include "command.h"
#include "mods.h"
#include "reflect.h"
#include "state.h"

static void emit(ScriptState& state, const char* name, const std::function<void(lua_State*)>& push_event) {
    auto it = state.handlers.find(name);
    if (it == state.handlers.end()) {
        return;
    }
    lua_State* lua = state.lua;
    for (LuaRef& fn : it->second) {
        if (!fn.isFunction()) {
            continue;
        }
        fn.push(lua);
        push_event(lua);
        int status = 0;
        {
            BudgetGuard guard(lua);
            status = lua_pcall(lua, 1, 0, 0);
        }
        if (status != 0) {
            SDL_Log("[lua] error in '%s': %s", name, lua_tostring(lua, -1));
            lua_pop(lua, 1);
        }
    }
}

static void emit_component_event(flecs::world world, std::unordered_map<std::string, std::vector<LuaRef>>& handlers, const std::string& name, flecs::entity entity) {
    auto it = handlers.find(name);
    if (it == handlers.end()) {
        return;
    }
    lua_State* lua = ScriptState::of(world).lua;
    for (LuaRef& fn : it->second) {
        if (!fn.isFunction()) {
            continue;
        }
        fn.push(lua);
        Lua::push(lua, ScriptEntity{.entity = entity});
        int status = 0;
        {
            BudgetGuard guard(lua);
            status = lua_pcall(lua, 1, 0, 0);
        }
        if (status != 0) {
            SDL_Log("[lua] component handler '%s' error: %s", name.c_str(), lua_tostring(lua, -1));
            lua_pop(lua, 1);
        }
    }
}

static void player_join(flecs::world world, flecs::entity peer, const std::string& username) {
    ScriptState::of(world).usernames[peer.id()] = username;
    emit(ScriptState::of(world), "player_join", [&](lua_State* lua) -> void {
        LuaRef ev = luabridge::newTable(lua);
        ev["player"] = ScriptPlayer{.peer = peer};
        ev.push(lua);
    });
}


static auto add_timer(flecs::world world, double seconds, const LuaRef& fn, bool repeat) -> int {
    ScriptState& state = ScriptState::of(world);
    const auto* clock = world.try_get<ServerClock>();
    uint64_t current = clock != nullptr ? clock->tick : 0;
    auto ticks = static_cast<uint64_t>(std::max<int64_t>(1, std::llround(seconds * 60.0)));
    int id = state.timer_next++;
    state.timers.push_back({.due = current + ticks, .interval = repeat ? ticks : 0, .id = id, .fn = fn});
    return id;
}

static void process_timers(ScriptState& state, uint64_t tick) {
    std::vector<LuaRef> due;
    auto& timers = state.timers;
    for (size_t i = 0; i < timers.size();) {
        if (timers[i].due <= tick) {
            due.push_back(timers[i].fn);
            if (timers[i].interval > 0) {
                timers[i].due += timers[i].interval;
                ++i;
            } else {
                timers.erase(timers.begin() + static_cast<std::ptrdiff_t>(i));
            }
        } else {
            ++i;
        }
    }
    lua_State* lua = state.lua;
    for (LuaRef& fn : due) {
        if (!fn.isFunction()) {
            continue;
        }
        Lua::push(lua, fn);
        int status = 0;
        {
            BudgetGuard guard(lua);
            status = lua_pcall(lua, 0, 0, 0);
        }
        if (status != 0) {
            SDL_Log("[lua] timer error: %s", lua_tostring(lua, -1));
            lua_pop(lua, 1);
        }
    }
}

static auto query_entities(flecs::world world, const LuaRef& names) -> std::vector<flecs::entity> {
    std::vector<flecs::entity> entities;
    auto gather = [](const LuaRef& src, std::vector<std::string>& out) -> void {
        std::string single = Reflect::component_ref_name(src);
        if (!single.empty()) {
            out.push_back(single);
        } else if (src.isTable()) {
            for (auto&& entry : luabridge::pairs(src)) {
                std::string n = Reflect::component_ref_name(entry.second);
                if (!n.empty()) {
                    out.push_back(n);
                }
            }
        }
    };
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    std::string single = Reflect::component_ref_name(names);
    if (!single.empty()) {
        include.push_back(single);
    } else if (names.isTable() && names["all"].isTable()) {
        gather(names["all"], include);
        gather(names["none"], exclude);
    } else {
        gather(names, include);
    }
    if (include.empty()) {
        return entities;
    }
    auto builder = world.query_builder();
    for (const std::string& n : include) {
        flecs::entity_t comp = Reflect::component_entity(world, n);
        if (comp == 0) {
            return entities;
        }
        builder.with(comp);
    }
    for (const std::string& n : exclude) {
        flecs::entity_t comp = Reflect::component_entity(world, n);
        if (comp != 0) {
            builder.without(comp);
        }
    }
    builder.build().each([&](flecs::entity e) -> void { entities.push_back(e); });
    return entities;
}

static void query_each(flecs::world world, const LuaRef& names, const LuaRef& fn) {
    if (!fn.isFunction()) {
        return;
    }
    lua_State* lua = ScriptState::of(world).lua;
    for (flecs::entity entity : query_entities(world, names)) {
        Lua::push(lua, fn);
        Lua::push(lua, ScriptEntity{.entity = entity});
        int status = 0;
        {
            BudgetGuard guard(lua);
            status = lua_pcall(lua, 1, 0, 0);
        }
        if (status != 0) {
            SDL_Log("[lua] query error: %s", lua_tostring(lua, -1));
            lua_pop(lua, 1);
        }
    }
}

struct EventType {
    const char* type;
    const char* payload;
};

static constexpr EventType EVENT_TYPES[] = {
    {"EventTick", "{ tick: number, dt: number }"},
    {"EventPlayerJoin", "{ player: Player }"},
    {"EventPlayerLeave", "{ player: Player }"},
    {"EventDeath", "{ entity: Entity }"},
    {"EventContact", "{ a: Entity, b: Entity }"},
    {"EventSensor", "{ a: Entity, b: Entity }"},
    {"EventContactEnd", "{ a: Entity, b: Entity }"},
    {"EventSensorEnd", "{ a: Entity, b: Entity }"},
};

static void generate_types(flecs::world world) {
    ScriptState& state = ScriptState::of(world);
    std::vector<std::string> names;
    names.reserve(state.components.size());
    for (const auto& [name, comp] : state.components) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());

    std::stringstream out;
    out << "-- AUTO-GENERATED by tankpvp scripting. Do not edit.\n\n";
    out << "export type Binding = { __bind: string }\n";
    out << "export type Vec = { x: number, y: number }\n";
    out << "export type RaycastHit = { entity: Entity, point: Vec, normal: Vec, fraction: number }\n";
    out << "export type AreaHit = { entity: Entity, distance: number }\n";
    out << "export type Tile = { read id: number }\n";
    out << "export type TileDef = { texture: string?, solid: boolean?, hp: number?, restitution: number?, friction: number?, drag: number? }\n";
    out << "declare function Tile(def: TileDef): Tile\n";
    out << "export type Sub = { cancel: (self: Sub) -> () }\n";
    out << "export type Signal<T> = { on: (self: Signal<T>, (T) -> ()) -> Sub, emit: (self: Signal<T>, T) -> () }\n";
    out << "export type Prototype<Def> = { define: (self: Prototype<Def>, string, Def) -> (), get: (self: Prototype<Def>, string) -> Def?, list: (self: Prototype<Def>) -> { string } }\n";
    auto partial_shape = [&](const std::string& comp) -> std::string {
        auto found = state.components.find(comp);
        if (found == state.components.end()) {
            return "any";
        }
        const auto* layout = ecs_get(world.c_ptr(), found->second, EcsStruct);
        if (layout == nullptr) {
            return "{}";
        }
        std::string shape;
        int32_t count = ecs_vec_count(&layout->members);
        for (int32_t i = 0; i < count; ++i) {
            auto* member = ecs_vec_get_t(&layout->members, ecs_member_t, i);
            const auto* primitive = ecs_get(world.c_ptr(), member->type, EcsPrimitive);
            const char* ty = (primitive != nullptr && primitive->kind == EcsString) ? "string" : "number";
            shape += (i == 0 ? "" : ", ") + std::string(member->name) + ": " + ty + "?";
        }
        return "{ " + shape + " }";
    };
    for (const auto& [name, keys] : state.proto_defs) {
        std::string def;
        for (const std::string& key : keys) {
            def += (def.empty() ? "" : ", ") + key + ": " + partial_shape(key) + "?";
        }
        out << "declare " << name << ": Prototype<{ " << def << " }>\n";
    }
    out << "export type Widget = { kind: string }\n";
    out << "export type Component<T> = T\n";
    out << "export type ComponentTag = {}\n";
    out << "export type Replicated<T> = T\n";
    out << "export type ReplicatedTag = {}\n";
    for (const auto& [type, lines] : state.api_types) {
        out << "export type " << type << " = {\n";
        for (const std::string& line : lines) {
            out << "\t" << line << ",\n";
        }
        out << "}\n";
    }
    out << "\n";

    for (const std::string& name : names) {
        flecs::entity_t comp = state.components[name];
        const auto* layout = ecs_get(world.c_ptr(), comp, EcsStruct);
        std::string value;
        std::string binding;
        std::vector<std::string> fields;
        int32_t count = layout != nullptr ? ecs_vec_count(&layout->members) : 0;
        for (int32_t i = 0; i < count; ++i) {
            auto* member = ecs_vec_get_t(&layout->members, ecs_member_t, i);
            const auto* primitive = ecs_get(world.c_ptr(), member->type, EcsPrimitive);
            const char* ty = (primitive != nullptr && primitive->kind == EcsString) ? "string" : "number";
            value += (i == 0 ? "" : ", ") + std::string(member->name) + ": " + ty;
            binding += (i == 0 ? "" : ", ") + std::string(member->name) + ": Binding";
            fields.emplace_back(member->name);
        }
        bool is_vec = fields.size() == 2 && fields[0] == "x" && fields[1] == "y";
        if (!state.author_components.contains(name) && name != "Replicated") {
            out << "export type " << name << " = " << (is_vec ? "Vec" : "{ " + value + " }") << "\n";
        }
        out << "declare " << name << ": { " << binding << " }\n";
    }

    out << "\n";
    for (const EventType& ev : EVENT_TYPES) {
        out << "export type " << ev.type << " = " << ev.payload << "\n";
    }
    for (const auto& [group, lines] : state.api_decls) {
        if (group.empty()) {
            for (const std::string& line : lines) {
                out << "declare " << line << "\n";
            }
        } else {
            out << "declare " << group << ": { ";
            for (size_t i = 0; i < lines.size(); ++i) {
                out << (i == 0 ? "" : ", ") << lines[i];
            }
            out << " }\n";
        }
    }

    std::ofstream file("mods/types.d.luau", std::ios::binary | std::ios::trunc);
    file << out.str();
}

static auto find_peer(flecs::entity tank) -> flecs::entity {
    flecs::entity found;
    tank.world().query_builder().with<Controls>(tank).build().each([&](flecs::entity peer) -> void {
        if (!found) {
            found = peer;
        }
    });
    return found;
}

static void wire_component_observers(flecs::world world) {
    ScriptState& state = ScriptState::of(world);
    auto wire = [&](std::unordered_map<std::string, std::vector<LuaRef>>& handlers, flecs::entity_t event, const char* tag) -> void {
        for (auto& entry : handlers) {
            const std::string& name = entry.first;
            std::string key = std::string(tag) + ":" + name;
            if (state.observed.count(key) != 0) {
                continue;
            }
            flecs::entity_t comp = Reflect::component_entity(world, name);
            if (comp == 0) {
                continue;
            }
            state.observed.insert(key);
            auto* map = &handlers;
            world.observer().with(comp).event(event).each([name, map](flecs::entity e) -> void { emit_component_event(e.world(), *map, name, e); });
        }
    };
    wire(state.component_handlers, flecs::OnSet, "set");
    wire(state.component_add_handlers, flecs::OnAdd, "add");
    wire(state.component_remove_handlers, flecs::OnRemove, "remove");
}

static void clear_loaded(ScriptState& state) {
    state.handlers.clear();
    state.component_handlers.clear();
    state.component_add_handlers.clear();
    state.component_remove_handlers.clear();
    state.signal_handlers.clear();
    state.commands.clear();
    state.prototypes.clear();
    state.proto_defs.clear();
    state.tile_id_next = 1;
    state.tile_rules.clear();
    state.tile_names.clear();
    state.generator.reset();
    state.view_owner.clear();
    state.timers.clear();
    state.inferred.clear();
    state.inferred_events.clear();
    state.inferred_next = 0;
    state.enum_aliases.clear();
    state.modules.clear();
    state.declared_this_load.clear();
}

static void unload_scripts(flecs::world world) {
    clear_loaded(ScriptState::of(world));
    world.remove<CommandBook>();
    SDL_Log("[script] unloaded mods");
}

static void reload_scripts(flecs::world world) {
    ScriptState& state = ScriptState::of(world);
    clear_loaded(state);
    Mods::load(world);
    std::vector<std::string> removed;
    for (const std::string& name : state.author_components) {
        if (!state.declared_this_load.contains(name)) {
            removed.push_back(name);
        }
    }
    for (const std::string& name : removed) {
        Reflect::remove_component(world, name);
    }
    Reflect::refresh_components(world);
    generate_types(world);
    wire_component_observers(world);
    world.set<CommandBook>({.commands = Command::command_list(world)});
    world.entity().add<RequestReload>();
    SDL_Log("[script] reloaded mods");
}

template <class F>
static void api_fn(luabridge::Namespace& ns, ScriptState& state, const std::string& group, const char* name, const char* sig, F&& fn) {
    ns.addFunction(name, std::forward<F>(fn));
    state.api_decls[group].push_back(std::string(name) + ": " + sig);
}

template <class Cls, class F>
static void api_method(Cls& cls, ScriptState& state, const char* type, const char* name, const char* sig, F&& fn) {
    cls.addFunction(name, std::forward<F>(fn));
    state.api_types[type].push_back(std::string(name) + ": " + sig);
}

template <class Cls, class G>
static void api_prop(Cls& cls, ScriptState& state, const char* type, const char* name, const char* luau, G&& getter) {
    cls.addProperty(name, std::forward<G>(getter));
    state.api_types[type].push_back(std::string(name) + ": " + luau);
}

static void setup_api(flecs::world world, lua_State* lua) {
    luaL_openlibs(lua);
    ScriptState& state = ScriptState::of(world);

    auto root = luabridge::getGlobalNamespace(lua);
    api_fn(root, state, "", "log", "(string) -> ()", [](const std::string& message) -> void { SDL_Log("[lua] %s", message.c_str()); });
    api_fn(root, state, "", "require", "(string) -> any", [world](const std::string& path) -> LuaRef { return Mods::require(world, path); });

    auto events = luabridge::getGlobalNamespace(lua).beginNamespace("events");
    api_fn(events, state, "events", "on", "(...any) -> Sub", [world](const LuaRef& handler, lua_State* s) -> LuaRef {
        ScriptState& st = ScriptState::of(world);
        if (handler.isFunction() && st.inferred_next < st.inferred_events.size()) {
            std::string name = st.inferred_events[st.inferred_next++];
            auto& vec = st.handlers[name];
            vec.push_back(handler);
            return LuaRef(s, ScriptSub{.kind = 0, .name = name, .slot = static_cast<int>(vec.size()) - 1});
        }
        return LuaRef(s);
    });
    api_fn(events, state, "events", "signal", "<T>() -> Signal<T>", [world](lua_State* s) -> LuaRef { return LuaRef(s, ScriptSignal{.id = ScriptState::of(world).signal_next++}); });
    auto on_component = [](flecs::world world, std::unordered_map<std::string, std::vector<LuaRef>>& map, int kind, const LuaRef& component, const LuaRef& handler, lua_State* s) -> LuaRef {
        std::string name = Reflect::component_ref_name(component);
        if (name.empty() || !handler.isFunction()) {
            return LuaRef(s);
        }
        auto& vec = map[name];
        vec.push_back(handler);
        return LuaRef(s, ScriptSub{.kind = kind, .name = name, .slot = static_cast<int>(vec.size()) - 1});
    };
    api_fn(events, state, "events", "on_set", "(any, (Entity) -> ()) -> Sub", [world, on_component](const LuaRef& component, const LuaRef& handler, lua_State* s) -> LuaRef { return on_component(world, ScriptState::of(world).component_handlers, 3, component, handler, s); });
    api_fn(events, state, "events", "on_add", "(any, (Entity) -> ()) -> Sub", [world, on_component](const LuaRef& component, const LuaRef& handler, lua_State* s) -> LuaRef { return on_component(world, ScriptState::of(world).component_add_handlers, 4, component, handler, s); });
    api_fn(events, state, "events", "on_remove", "(any, (Entity) -> ()) -> Sub", [world, on_component](const LuaRef& component, const LuaRef& handler, lua_State* s) -> LuaRef { return on_component(world, ScriptState::of(world).component_remove_handlers, 5, component, handler, s); });
    api_fn(events, state, "events", "on_tile", "(Tile, (number, number) -> ()) -> ()", [world](const ScriptTile& tile, const LuaRef& fn) -> void {
        if (fn.isFunction()) {
            ScriptState::of(world).tile_rules.insert_or_assign(tile.id, fn);
        }
    });
    events.endNamespace();

    auto worldns = luabridge::getGlobalNamespace(lua).beginNamespace("world");
    api_fn(worldns, state, "world", "broadcast", "(string) -> ()", [world](const std::string& message) -> void { world.entity().set(RequestBroadcast{.line = message}); });
    api_fn(worldns, state, "world", "reload", "() -> ()", [world]() -> void { ScriptState::of(world).reload_pending = true; });
    api_fn(worldns, state, "world", "generator", "((number, number) -> ()) -> ()", [world](const LuaRef& fn) -> void {
        if (fn.isFunction()) {
            ScriptState::of(world).generator = fn;
        }
    });
    api_fn(worldns, state, "world", "set_tile", "(number, number, Tile) -> ()", [world](double x, double y, const ScriptTile& tile) -> void {
        world.entity().set(RequestSetTile{.tx = static_cast<int32_t>(std::floor(x)), .ty = static_cast<int32_t>(std::floor(y)), .id = tile.id});
    });
    api_fn(worldns, state, "world", "clear_tile", "(number, number) -> ()", [world](double x, double y) -> void {
        world.entity().set(RequestSetTile{.tx = static_cast<int32_t>(std::floor(x)), .ty = static_cast<int32_t>(std::floor(y)), .id = TILE_EMPTY});
    });
    api_fn(worldns, state, "world", "get_tile", "(number, number) -> number", [world](double x, double y) -> double {
        const auto* grid = world.try_get<WorldGrid>();
        if (grid == nullptr) {
            return 0.0;
        }
        int tx = static_cast<int>(std::floor(x));
        int ty = static_cast<int>(std::floor(y));
        auto [cx, cy] = WorldGrid::chunk_coord(tx, ty);
        auto it = grid->data.find(WorldGrid::key(cx, cy));
        return it == grid->data.end() ? 0.0 : static_cast<double>(it->second.tiles[WorldGrid::local_index(tx, ty)]);
    });
    api_fn(worldns, state, "world", "each", "(any, (Entity) -> ()) -> ()", [world](const LuaRef& names, const LuaRef& fn) -> void { query_each(world, names, fn); });
    api_fn(worldns, state, "world", "spawn", "(any) -> Entity", [world](const LuaRef& def, lua_State* s) -> LuaRef {
        flecs::entity e = world.entity();
        if (def.isTable()) {
            for (auto&& entry : luabridge::pairs(def)) {
                std::string name = Reflect::component_ref_name(entry.first);
                flecs::entity_t comp = name.empty() ? 0 : Reflect::component_entity(world, name);
                if (comp == 0) {
                    continue;
                }
                if (ecs_get(world.c_ptr(), comp, EcsStruct) != nullptr && entry.second.isTable()) {
                    Reflect::set_component_from_ref(e, name, entry.second);
                } else {
                    ecs_add_id(world.c_ptr(), e.id(), comp);
                }
            }
        }
        return LuaRef(s, ScriptEntity{.entity = e});
    });
    api_fn(worldns, state, "world", "entity", "(number) -> Entity?", [world](double id, lua_State* s) -> LuaRef {
        flecs::entity e = world.entity(static_cast<flecs::entity_t>(id));
        return e.is_alive() ? LuaRef(s, ScriptEntity{.entity = e}) : LuaRef(s);
    });
    api_fn(worldns, state, "world", "players", "() -> { Player }", [world](lua_State* s) -> LuaRef {
        LuaRef out = luabridge::newTable(s);
        int index = 1;
        world.query_builder().with<Tank>().with<Owner>().build().each([&](flecs::entity tank) -> void {
            flecs::entity peer = find_peer(tank);
            if (peer) {
                out[index++] = ScriptPlayer{.peer = peer};
            }
        });
        return out;
    });
    api_fn(worldns, state, "world", "find", "(any) -> Entity?", [world](const LuaRef& filter, lua_State* s) -> LuaRef {
        std::vector<flecs::entity> entities = query_entities(world, filter);
        return entities.empty() ? LuaRef(s) : LuaRef(s, ScriptEntity{.entity = entities.front()});
    });
    api_fn(worldns, state, "world", "count", "(any) -> number", [world](const LuaRef& filter) -> double { return static_cast<double>(query_entities(world, filter).size()); });
    api_fn(worldns, state, "world", "player", "(string) -> Player?", [world](const std::string& name, lua_State* s) -> LuaRef {
        for (const auto& [id, uname] : ScriptState::of(world).usernames) {
            if (uname == name) {
                flecs::entity peer = world.entity(id);
                if (peer.is_alive()) {
                    return LuaRef(s, ScriptPlayer{.peer = peer});
                }
            }
        }
        return LuaRef(s);
    });
    api_fn(worldns, state, "world", "explosion", "({ center: Vec, radius: number, force: number, damage: number }) -> ()", [world](const LuaRef& spec) -> void {
        LuaRef c = spec["center"];
        world.entity().set(RequestExplosion{
            .center = glm::vec2(static_cast<float>(Lua::ref_number(c, "x", 0.0)), static_cast<float>(Lua::ref_number(c, "y", 0.0))),
            .radius = static_cast<float>(Lua::ref_number(spec, "radius", 0.0)),
            .force = static_cast<float>(Lua::ref_number(spec, "force", 0.0)),
            .damage = static_cast<float>(Lua::ref_number(spec, "damage", 0.0)),
        });
    });
    api_fn(worldns, state, "world", "raycast", "({ origin: Vec, direction: Vec, range: number }, ({ RaycastHit }) -> ()) -> ()", [world](const LuaRef& spec, const LuaRef& callback) -> void {
        LuaRef o = spec["origin"];
        LuaRef d = spec["direction"];
        flecs::entity e = world.entity();
        e.set(RequestRaycast{
            .origin = glm::vec2(static_cast<float>(Lua::ref_number(o, "x", 0.0)), static_cast<float>(Lua::ref_number(o, "y", 0.0))),
            .direction = glm::vec2(static_cast<float>(Lua::ref_number(d, "x", 0.0)), static_cast<float>(Lua::ref_number(d, "y", 0.0))),
            .range = static_cast<float>(Lua::ref_number(spec, "range", 0.0)),
        });
        ScriptState::of(world).query_callbacks.insert_or_assign(e.id(), callback);
    });
    api_fn(worldns, state, "world", "area", "({ center: Vec, radius: number }, ({ AreaHit }) -> ()) -> ()", [world](const LuaRef& spec, const LuaRef& callback) -> void {
        LuaRef c = spec["center"];
        flecs::entity e = world.entity();
        e.set(RequestAreaQuery{
            .center = glm::vec2(static_cast<float>(Lua::ref_number(c, "x", 0.0)), static_cast<float>(Lua::ref_number(c, "y", 0.0))),
            .radius = static_cast<float>(Lua::ref_number(spec, "radius", 0.0)),
        });
        ScriptState::of(world).query_callbacks.insert_or_assign(e.id(), callback);
    });
    worldns.endNamespace();

    auto vecns = luabridge::getGlobalNamespace(lua).beginNamespace("vec");
    api_fn(vecns, state, "vec", "distance", "(Vec, Vec) -> number", [](const LuaRef& a, const LuaRef& b) -> double {
        double dx = Lua::ref_number(a, "x", 0.0) - Lua::ref_number(b, "x", 0.0);
        double dy = Lua::ref_number(a, "y", 0.0) - Lua::ref_number(b, "y", 0.0);
        return std::sqrt((dx * dx) + (dy * dy));
    });
    api_fn(vecns, state, "vec", "length", "(Vec) -> number", [](const LuaRef& v) -> double {
        double x = Lua::ref_number(v, "x", 0.0);
        double y = Lua::ref_number(v, "y", 0.0);
        return std::sqrt((x * x) + (y * y));
    });
    api_fn(vecns, state, "vec", "angle", "(Vec, Vec) -> number", [](const LuaRef& a, const LuaRef& b) -> double {
        return std::atan2(Lua::ref_number(b, "y", 0.0) - Lua::ref_number(a, "y", 0.0), Lua::ref_number(b, "x", 0.0) - Lua::ref_number(a, "x", 0.0));
    });
    api_fn(vecns, state, "vec", "dot", "(Vec, Vec) -> number", [](const LuaRef& a, const LuaRef& b) -> double {
        return (Lua::ref_number(a, "x", 0.0) * Lua::ref_number(b, "x", 0.0)) + (Lua::ref_number(a, "y", 0.0) * Lua::ref_number(b, "y", 0.0));
    });
    api_fn(vecns, state, "vec", "lerp", "(Vec, Vec, number) -> Vec", [](const LuaRef& a, const LuaRef& b, double t, lua_State* s) -> LuaRef {
        LuaRef out = luabridge::newTable(s);
        out["x"] = Lua::ref_number(a, "x", 0.0) + ((Lua::ref_number(b, "x", 0.0) - Lua::ref_number(a, "x", 0.0)) * t);
        out["y"] = Lua::ref_number(a, "y", 0.0) + ((Lua::ref_number(b, "y", 0.0) - Lua::ref_number(a, "y", 0.0)) * t);
        return out;
    });
    api_fn(vecns, state, "vec", "normalize", "(Vec) -> Vec", [](const LuaRef& v, lua_State* s) -> LuaRef {
        double x = Lua::ref_number(v, "x", 0.0);
        double y = Lua::ref_number(v, "y", 0.0);
        double len = std::sqrt((x * x) + (y * y));
        LuaRef out = luabridge::newTable(s);
        out["x"] = len > 0.0 ? x / len : 0.0;
        out["y"] = len > 0.0 ? y / len : 0.0;
        return out;
    });
    vecns.endNamespace();

    auto audions = luabridge::getGlobalNamespace(lua).beginNamespace("audio");
    api_fn(audions, state, "audio", "play", "(string, { at: Vec?, volume: number? }?) -> ()", [world](const std::string& name, const LuaRef& opts) -> void {
        RequestSound sound{.asset = name};
        if (opts.isTable()) {
            LuaRef at = opts["at"];
            if (at.isTable()) {
                sound.x = static_cast<float>(Lua::ref_number(at, "x", 0.0));
                sound.y = static_cast<float>(Lua::ref_number(at, "y", 0.0));
            }
            sound.volume = static_cast<float>(Lua::ref_number(opts, "volume", 1.0));
        }
        world.entity().set(sound);
    });
    audions.endNamespace();

    auto schedulens = luabridge::getGlobalNamespace(lua).beginNamespace("schedule");
    api_fn(schedulens, state, "schedule", "after", "(number, () -> ()) -> Sub", [world](double seconds, const LuaRef& fn, lua_State* s) -> LuaRef { return LuaRef(s, ScriptSub{.kind = 2, .slot = add_timer(world, seconds, fn, false)}); });
    api_fn(schedulens, state, "schedule", "every", "(number, () -> ()) -> Sub", [world](double seconds, const LuaRef& fn, lua_State* s) -> LuaRef { return LuaRef(s, ScriptSub{.kind = 2, .slot = add_timer(world, seconds, fn, true)}); });
    schedulens.endNamespace();

    auto commandns = luabridge::getGlobalNamespace(lua).beginNamespace("command");
    api_fn(commandns, state, "command", "register", "(string, any) -> ()", [world](const std::string& name, const LuaRef& spec) -> void {
        ScriptState& st = ScriptState::of(world);
        st.commands.insert_or_assign(name, spec);
        LuaRef aliases = spec["aliases"];
        if (aliases.isTable()) {
            auto inferred = st.inferred.find(name);
            for (auto&& entry : luabridge::pairs(aliases)) {
                if (entry.second.isString()) {
                    std::string alias = entry.second.unsafe_cast<std::string>();
                    st.commands.insert_or_assign(alias, spec);
                    if (inferred != st.inferred.end()) {
                        st.inferred[alias] = inferred->second;
                    }
                }
            }
        }
    });
    api_fn(commandns, state, "command", "unregister", "(string) -> ()", [world](const std::string& name) -> void { ScriptState::of(world).commands.erase(name); });
    commandns.endNamespace();

    luabridge::getGlobalNamespace(lua)
        .beginClass<ScriptProto>("Prototype")
        .addFunction("define", [world](const ScriptProto* self, const std::string& name, const LuaRef& def) -> void { ScriptState::of(world).prototypes[self->category].insert_or_assign(name, def); })
        .addFunction("get", [world](const ScriptProto* self, const std::string& name, lua_State* s) -> LuaRef {
            auto& cats = ScriptState::of(world).prototypes;
            auto cit = cats.find(self->category);
            if (cit != cats.end()) {
                auto nit = cit->second.find(name);
                if (nit != cit->second.end()) {
                    return nit->second;
                }
            }
            return {s};
        })
        .addFunction("list", [world](const ScriptProto* self, lua_State* s) -> LuaRef {
            LuaRef out = luabridge::newTable(s);
            auto& cats = ScriptState::of(world).prototypes;
            auto cit = cats.find(self->category);
            int index = 1;
            if (cit != cats.end()) {
                for (const auto& [name, def] : cit->second) {
                    out[index++] = name;
                }
            }
            return out;
        })
        .endClass();

    luabridge::getGlobalNamespace(lua).beginClass<ScriptTile>("TileHandle").addProperty("id", +[](const ScriptTile* self) -> double { return static_cast<double>(self->id); }).endClass();
    luabridge::getGlobalNamespace(lua).addFunction("Tile", [world](const LuaRef& def) -> ScriptTile {
        TileType type;
        std::string texture;
        if (def.isTable()) {
            if (LuaRef t = def["texture"]; t.isString()) {
                texture = t.unsafe_cast<std::string>();
            }
            if (LuaRef v = def["solid"]; v.isBool()) { type.solid = v.unsafe_cast<bool>(); }
            if (LuaRef v = def["restitution"]; v.isNumber()) { type.restitution = static_cast<float>(v.unsafe_cast<double>()); }
            if (LuaRef v = def["friction"]; v.isNumber()) { type.friction = static_cast<float>(v.unsafe_cast<double>()); }
            if (LuaRef v = def["drag"]; v.isNumber()) { type.drag = static_cast<float>(v.unsafe_cast<double>()); }
            if (LuaRef v = def["hp"]; v.isNumber()) { type.hp = static_cast<int32_t>(v.unsafe_cast<double>()); }
            for (auto&& entry : luabridge::pairs(def)) {
                if (!entry.first.isString()) {
                    continue;
                }
                std::string key = entry.first.unsafe_cast<std::string>();
                if (key != "texture" && key != "solid" && key != "restitution" && key != "friction" && key != "drag" && key != "hp") {
                    SDL_Log("[lua] Tile: unknown field '%s'", key.c_str());
                }
            }
        }
        auto& st = ScriptState::of(world);
        uint16_t id = st.tile_id_next++;
        std::string name = (id >= 1 && static_cast<size_t>(id - 1) < st.tile_names.size()) ? st.tile_names[id - 1] : std::string{};
        world.entity().set(RequestDefineTile{.id = id, .type = type, .texture = texture, .name = name});
        return ScriptTile{.id = id};
    });

    auto viewns = luabridge::getGlobalNamespace(lua).beginNamespace("view");
    api_fn(viewns, state, "view", "label", "(any) -> Widget", [](const LuaRef& value, lua_State* s) -> LuaRef {
        LuaRef table = luabridge::newTable(s);
        table["kind"] = "label";
        table["value"] = value;
        return table;
    });
    api_fn(viewns, state, "view", "button", "(string, (Context) -> ()) -> Widget", [](const std::string& text, const LuaRef& fn, lua_State* s) -> LuaRef {
        LuaRef table = luabridge::newTable(s);
        table["kind"] = "button";
        table["text"] = text;
        table["on_click"] = fn;
        return table;
    });
    api_fn(viewns, state, "view", "bar", "(any) -> Widget", [](LuaRef table) -> LuaRef {
        table["kind"] = "bar";
        return table;
    });
    api_fn(viewns, state, "view", "input", "(any) -> Widget", [](LuaRef table) -> LuaRef {
        table["kind"] = "input";
        return table;
    });
    api_fn(viewns, state, "view", "slider", "(any) -> Widget", [](LuaRef table) -> LuaRef {
        table["kind"] = "slider";
        return table;
    });
    api_fn(viewns, state, "view", "toggle", "(any) -> Widget", [](LuaRef table) -> LuaRef {
        table["kind"] = "toggle";
        return table;
    });
    api_fn(viewns, state, "view", "panel", "(any) -> Widget", [](LuaRef table) -> LuaRef {
        table["kind"] = "panel";
        return table;
    });
    api_fn(viewns, state, "view", "row", "(any) -> Widget", [](LuaRef table) -> LuaRef {
        table["kind"] = "panel";
        table["layout"] = "row";
        return table;
    });
    api_fn(viewns, state, "view", "column", "(any) -> Widget", [](LuaRef table) -> LuaRef {
        table["kind"] = "panel";
        table["layout"] = "column";
        return table;
    });
    api_fn(viewns, state, "view", "spacer", "(number?) -> Widget", [](const LuaRef& size, lua_State* s) -> LuaRef {
        LuaRef table = luabridge::newTable(s);
        table["kind"] = "spacer";
        table["value"] = size.isNumber() ? size.unsafe_cast<double>() : 8.0;
        return table;
    });
    api_fn(viewns, state, "view", "separator", "() -> Widget", [](lua_State* s) -> LuaRef {
        LuaRef table = luabridge::newTable(s);
        table["kind"] = "separator";
        return table;
    });
    api_fn(viewns, state, "view", "each", "<T>({ T }, (T) -> Widget) -> Widget", [](const LuaRef& items, const LuaRef& builder, lua_State* s) -> LuaRef {
        LuaRef table = luabridge::newTable(s);
        table["kind"] = "each";
        table["items"] = items;
        table["builder"] = builder;
        return table;
    });
    viewns.endNamespace();

    auto entity = luabridge::getGlobalNamespace(lua).beginClass<ScriptEntity>("Entity");
    api_method(entity, state, "Entity", "id", "(self: Entity) -> number", [](const ScriptEntity* self) -> double { return static_cast<double>(self->entity.id()); });
    api_method(entity, state, "Entity", "alive", "(self: Entity) -> boolean", [](const ScriptEntity* self) -> bool { return self->entity.is_alive(); });
    api_method(entity, state, "Entity", "destroy", "(self: Entity) -> ()", [](const ScriptEntity* self) -> void {
        if (self->entity.is_alive()) {
            self->entity.destruct();
        }
    });
    api_method(entity, state, "Entity", "has", "(self: Entity, any) -> boolean", [](const ScriptEntity* self, const LuaRef& component) -> bool { return self->entity.is_alive() && Reflect::entity_has_component(self->entity, Reflect::component_ref_name(component)); });
    api_method(entity, state, "Entity", "get", "(self: Entity, any) -> any", [](const ScriptEntity* self, const LuaRef& component, lua_State* s) -> LuaRef { return Reflect::component_to_ref(s, self->entity, Reflect::component_ref_name(component)); });
    api_method(entity, state, "Entity", "set", "(self: Entity, any, any) -> ()", [](const ScriptEntity* self, const LuaRef& component, const LuaRef& value) -> void { Reflect::set_component_from_ref(self->entity, Reflect::component_ref_name(component), value); });
    api_method(entity, state, "Entity", "add", "(self: Entity, any) -> ()", [](const ScriptEntity* self, const LuaRef& component) -> void {
        flecs::world world = self->entity.world();
        flecs::entity_t comp = Reflect::component_entity(world, Reflect::component_ref_name(component));
        if (comp != 0 && self->entity.is_alive()) {
            ecs_add_id(world.c_ptr(), self->entity.id(), comp);
        }
    });
    api_method(entity, state, "Entity", "remove", "(self: Entity, any) -> ()", [](const ScriptEntity* self, const LuaRef& component) -> void {
        flecs::world world = self->entity.world();
        flecs::entity_t comp = Reflect::component_entity(world, Reflect::component_ref_name(component));
        if (comp != 0 && self->entity.is_alive()) {
            ecs_remove_id(world.c_ptr(), self->entity.id(), comp);
        }
    });
    api_method(entity, state, "Entity", "player", "(self: Entity) -> Player?", [](const ScriptEntity* self, lua_State* s) -> LuaRef {
        flecs::entity peer = find_peer(self->entity);
        return peer ? LuaRef(s, ScriptPlayer{.peer = peer}) : LuaRef(s);
    });
    api_method(entity, state, "Entity", "parent", "(self: Entity) -> Entity?", [](const ScriptEntity* self, lua_State* s) -> LuaRef {
        flecs::entity p = self->entity.parent();
        return (p && p.is_alive()) ? LuaRef(s, ScriptEntity{.entity = p}) : LuaRef(s);
    });
    api_method(entity, state, "Entity", "children", "(self: Entity) -> { Entity }", [](const ScriptEntity* self, lua_State* s) -> LuaRef {
        LuaRef out = luabridge::newTable(s);
        int index = 1;
        self->entity.children([&](flecs::entity child) -> void { out[index++] = ScriptEntity{.entity = child}; });
        return out;
    });
    api_method(entity, state, "Entity", "set_parent", "(self: Entity, Entity) -> ()", [](const ScriptEntity* self, const ScriptEntity* parent) -> void {
        if (parent != nullptr) {
            self->entity.child_of(parent->entity);
        }
    });

    auto set_layer = [](const ScriptEntity* self, int index, const std::string& name) -> void {
        if (index < 0 || index >= SPRITE_LAYERS) {
            return;
        }
        flecs::world world = self->entity.world();
        const auto* catalog = world.try_get<AssetCatalog>();
        if (catalog == nullptr) {
            return;
        }
        uint64_t hash = catalog->hash_of(name);
        if (hash == 0) {
            return;
        }
        Sprite s = self->entity.has<Sprite>() ? self->entity.get<Sprite>() : Sprite{};
        s.texture[index] = hash;
        self->entity.set<Sprite>(s);
    };

    api_method(entity, state, "Entity", "sprite", "(self: Entity, string | { { tex: string, pivot: { number }?, offset: { number }? } }) -> ()", [set_layer](const ScriptEntity* self, const LuaRef& spec) -> void {
        if (spec.isString()) {
            set_layer(self, 0, spec.unsafe_cast<std::string>());
            return;
        }
        if (!spec.isTable()) {
            return;
        }
        const auto* catalog = self->entity.world().try_get<AssetCatalog>();
        if (catalog == nullptr) {
            return;
        }
        Sprite s{};
        int count = spec.length();
        for (int i = 1; i <= count && i <= SPRITE_LAYERS; ++i) {
            LuaRef layer = spec[i];
            if (!layer.isTable()) {
                continue;
            }
            LuaRef tex = layer["tex"];
            if (!tex.isString()) {
                continue;
            }
            uint64_t hash = catalog->hash_of(tex.unsafe_cast<std::string>());
            if (hash == 0) {
                continue;
            }
            s.texture[i - 1] = hash;
            if (LuaRef pivot = layer["pivot"]; pivot.isTable() && pivot.length() >= 2) {
                s.pivot_x[i - 1] = static_cast<float>(pivot[1].unsafe_cast<double>()) - 0.5F;
                s.pivot_y[i - 1] = static_cast<float>(pivot[2].unsafe_cast<double>()) - 0.5F;
            }
            if (LuaRef offset = layer["offset"]; offset.isTable() && offset.length() >= 2) {
                s.offset_x[i - 1] = static_cast<float>(offset[1].unsafe_cast<double>());
                s.offset_y[i - 1] = static_cast<float>(offset[2].unsafe_cast<double>());
            }
        }
        self->entity.set<Sprite>(s);
    });
    api_method(entity, state, "Entity", "layer", "(self: Entity, number, string) -> ()", [set_layer](const ScriptEntity* self, int index, const std::string& name) -> void { set_layer(self, index - 1, name); });
    api_method(entity, state, "Entity", "apply", "(self: Entity, any) -> ()", [](const ScriptEntity* self, const LuaRef& def) -> void {
        if (!def.isTable()) {
            return;
        }
        for (auto&& entry : luabridge::pairs(def)) {
            std::string name = Reflect::component_ref_name(entry.first);
            if (!name.empty() && entry.second.isTable()) {
                Reflect::set_component_from_ref(self->entity, name, entry.second);
            }
        }
    });
    state.api_types["Entity"].push_back("[string]: any");
    entity
        .addIndexMetaMethod([](ScriptEntity& self, const LuaRef& key, lua_State* s) -> LuaRef {
            std::string name = key.tostring();
            return Reflect::is_known_component(self.entity.world(), name) ? Reflect::component_to_ref(s, self.entity, name) : LuaRef(s);
        })
        .addNewIndexMetaMethod([](ScriptEntity& self, const LuaRef& key, const LuaRef& value, lua_State* s) -> LuaRef {
            std::string name = key.tostring();
            if (Reflect::is_known_component(self.entity.world(), name)) {
                Reflect::set_component_from_ref(self.entity, name, value);
            }
            return {s};
        })
        .endClass();

    auto context = luabridge::getGlobalNamespace(lua).beginClass<ScriptContext>("Context");
    api_prop(context, state, "Context", "name", "string", &ScriptContext::name);
    api_prop(context, state, "Context", "player", "Player?", +[](const ScriptContext* self) -> LuaRef { return self->player; });
    api_prop(context, state, "Context", "form", "{ [string]: string }", +[](const ScriptContext* self) -> LuaRef { return self->form; });
    api_method(context, state, "Context", "reply", "(self: Context, string) -> ()", &ScriptContext::reply);
    api_method(context, state, "Context", "open_view", "(self: Context, string, Widget) -> ()", &ScriptContext::open_view);
    api_method(context, state, "Context", "close_view", "(self: Context, string) -> ()", &ScriptContext::close_view);
    api_method(context, state, "Context", "kick", "(self: Context, string) -> ()", &ScriptContext::kick);
    context.endClass();

    auto player = luabridge::getGlobalNamespace(lua).beginClass<ScriptPlayer>("Player");
    api_method(player, state, "Player", "reply", "(self: Player, string) -> ()", [](const ScriptPlayer* self, const std::string& message) -> void { self->peer.world().entity().set(RequestReply{.peer = self->peer, .line = message}); });
    api_method(player, state, "Player", "kick", "(self: Player, string) -> ()", [](const ScriptPlayer* self, const std::string& reason) -> void { self->peer.world().entity().set(RequestKick{.peer = self->peer, .reason = reason}); });
    api_method(player, state, "Player", "open_view", "(self: Player, string, Widget) -> ()", [](const ScriptPlayer* self, const std::string& id, const LuaRef& tree) -> void { Command::open_view(self->peer.world(), self->peer, id, tree); });
    api_method(player, state, "Player", "close_view", "(self: Player, string) -> ()", [](const ScriptPlayer* self, const std::string& id) -> void { Command::close_view(self->peer.world(), self->peer, id); });
    api_method(player, state, "Player", "entity", "(self: Player) -> Entity?", [](const ScriptPlayer* self, lua_State* s) -> LuaRef {
        flecs::entity tank = self->peer.target<Controls>();
        return (tank && tank.is_alive()) ? LuaRef(s, ScriptEntity{.entity = tank}) : LuaRef(s);
    });
    api_method(player, state, "Player", "name", "(self: Player) -> string", [](const ScriptPlayer* self) -> std::string {
        auto& names = ScriptState::of(self->peer.world()).usernames;
        auto it = names.find(self->peer.id());
        return it != names.end() ? it->second : std::string();
    });
    player.endClass();

    luabridge::getGlobalNamespace(lua)
        .beginClass<ScriptSignal>("Signal")
        .addFunction("on", [world](const ScriptSignal* self, const LuaRef& fn, lua_State* s) -> LuaRef {
            if (fn.isFunction()) {
                auto& vec = ScriptState::of(world).signal_handlers[self->id];
                vec.push_back(fn);
                return LuaRef(s, ScriptSub{.kind = 1, .name = std::to_string(self->id), .slot = static_cast<int>(vec.size()) - 1});
            }
            return LuaRef(s);
        })
        .addFunction("emit", [world](const ScriptSignal* self, const LuaRef& payload) -> void {
            ScriptState& st = ScriptState::of(world);
            auto it = st.signal_handlers.find(self->id);
            if (it == st.signal_handlers.end()) {
                return;
            }
            lua_State* lua = st.lua;
            for (LuaRef& fn : it->second) {
                if (!fn.isFunction()) {
                    continue;
                }
                fn.push(lua);
                payload.push(lua);
                int status = 0;
                {
                    BudgetGuard guard(lua);
                    status = lua_pcall(lua, 1, 0, 0);
                }
                if (status != 0) {
                    SDL_Log("[lua] signal handler error: %s", lua_tostring(lua, -1));
                    lua_pop(lua, 1);
                }
            }
        })
        .endClass();

    luabridge::getGlobalNamespace(lua)
        .beginClass<ScriptSub>("Sub")
        .addFunction("cancel", [world](const ScriptSub* self) -> void {
            ScriptState& st = ScriptState::of(world);
            auto nil_slot = [&](std::unordered_map<std::string, std::vector<LuaRef>>& map) -> void {
                auto it = map.find(self->name);
                if (it != map.end() && self->slot >= 0 && self->slot < static_cast<int>(it->second.size())) {
                    it->second[self->slot] = LuaRef(st.lua);
                }
            };
            switch (self->kind) {
                case 0:
                    nil_slot(st.handlers);
                    break;
                case 1: {
                    auto it = st.signal_handlers.find(std::stoi(self->name));
                    if (it != st.signal_handlers.end() && self->slot >= 0 && self->slot < static_cast<int>(it->second.size())) {
                        it->second[self->slot] = LuaRef(st.lua);
                    }
                    break;
                }
                case 2: {
                    auto& timers = st.timers;
                    timers.erase(std::remove_if(timers.begin(), timers.end(), [&](const ScriptTimer& t) -> bool { return t.id == self->slot; }), timers.end());
                    break;
                }
                case 3:
                    nil_slot(st.component_handlers);
                    break;
                case 4:
                    nil_slot(st.component_add_handlers);
                    break;
                case 5:
                    nil_slot(st.component_remove_handlers);
                    break;
                default:
                    break;
            }
        })
        .endClass();
}

Script::Script(flecs::world& world) {
    world.component<ScriptState>().add(flecs::Singleton);
    world.set<ScriptState>(ScriptState{});

    ScriptState& state = world.get_mut<ScriptState>();
    state.lua = luaL_newstate();
    setup_api(world, state.lua);
    Reflect::refresh_components(world);
    Reflect::register_component(world, "Replicated", world.component<Replicated>().id());

    world.observer().with<Dying>().event(flecs::OnAdd).each([](flecs::entity e) -> void {
        flecs::world w = e.world();
        emit(ScriptState::of(w), "death", [&](lua_State* lua) -> void {
            LuaRef ev = luabridge::newTable(lua);
            ev["entity"] = ScriptEntity{.entity = e};
            ev.push(lua);
        });
    });

    world.observer<const ResponseRaycast>("script::raycast_result").event(flecs::OnSet).each([](flecs::entity e, const ResponseRaycast& r) -> void {
        ScriptState& st = ScriptState::of(e.world());
        auto it = st.query_callbacks.find(e.id());
        if (it != st.query_callbacks.end()) {
            lua_State* lua = st.lua;
            if (it->second.isFunction()) {
                it->second.push(lua);
                LuaRef hits = luabridge::newTable(lua);
                int index = 1;
                for (const ResponseRaycast::Hit& hit : r.hits) {
                    LuaRef h = luabridge::newTable(lua);
                    h["entity"] = ScriptEntity{.entity = hit.entity};
                    LuaRef point = luabridge::newTable(lua);
                    point["x"] = hit.point.x;
                    point["y"] = hit.point.y;
                    h["point"] = point;
                    LuaRef normal = luabridge::newTable(lua);
                    normal["x"] = hit.normal.x;
                    normal["y"] = hit.normal.y;
                    h["normal"] = normal;
                    h["fraction"] = hit.fraction;
                    hits[index++] = h;
                }
                hits.push(lua);
                int status = 0;
                {
                    BudgetGuard guard(lua);
                    status = lua_pcall(lua, 1, 0, 0);
                }
                if (status != 0) {
                    SDL_Log("[lua] raycast callback error: %s", lua_tostring(lua, -1));
                    lua_pop(lua, 1);
                }
            }
            st.query_callbacks.erase(it);
        }
        e.destruct();
    });
    world.observer<const ResponseAreaQuery>("script::area_result").event(flecs::OnSet).each([](flecs::entity e, const ResponseAreaQuery& r) -> void {
        ScriptState& st = ScriptState::of(e.world());
        auto it = st.query_callbacks.find(e.id());
        if (it != st.query_callbacks.end()) {
            lua_State* lua = st.lua;
            if (it->second.isFunction()) {
                it->second.push(lua);
                LuaRef hits = luabridge::newTable(lua);
                int index = 1;
                for (const ResponseAreaQuery::Hit& hit : r.hits) {
                    LuaRef h = luabridge::newTable(lua);
                    h["entity"] = ScriptEntity{.entity = hit.entity};
                    h["distance"] = hit.distance;
                    hits[index++] = h;
                }
                hits.push(lua);
                int status = 0;
                {
                    BudgetGuard guard(lua);
                    status = lua_pcall(lua, 1, 0, 0);
                }
                if (status != 0) {
                    SDL_Log("[lua] area callback error: %s", lua_tostring(lua, -1));
                    lua_pop(lua, 1);
                }
            }
            st.query_callbacks.erase(it);
        }
        e.destruct();
    });

    world.system("script::tick").kind(flecs::OnUpdate).immediate().run([](flecs::iter& it) -> void {
        while (it.next()) {
            flecs::world world = it.world();
            const auto* clock = world.try_get<ServerClock>();
            if (clock == nullptr || !clock->running) {
                continue;
            }
            uint64_t tick = clock->tick;
            ScriptState& state = world.get_mut<ScriptState>();
            if (state.reload_pending) {
                state.reload_pending = false;
                reload_scripts(world);
                continue;
            }
            emit(state, "tick", [&](lua_State* lua) -> void {
                LuaRef ev = luabridge::newTable(lua);
                ev["tick"] = static_cast<double>(tick);
                ev["dt"] = 1.0 / 60.0;
                ev.push(lua);
            });
            process_timers(state, tick);
            if (const auto* events = world.try_get<PhysicsEvents>()) {
                auto emit_contacts = [&](const auto& buffer, const char* name) -> void {
                    for (const ContactEvent& c : buffer) {
                        if (!c.entity_a.is_alive() || !c.entity_b.is_alive()) {
                            continue;
                        }
                        emit(state, name, [&](lua_State* lua) -> void {
                            LuaRef ev = luabridge::newTable(lua);
                            ev["a"] = ScriptEntity{.entity = c.entity_a};
                            ev["b"] = ScriptEntity{.entity = c.entity_b};
                            ev.push(lua);
                        });
                    }
                };
                emit_contacts(events->contactBegin, "contact");
                emit_contacts(events->sensorBegin, "sensor");
                emit_contacts(events->contactEnd, "contact_end");
                emit_contacts(events->sensorEnd, "sensor_end");
            }
        }
    });

    world.observer<const RequestCommand>("script::command").event(flecs::OnSet).each([](flecs::entity e, const RequestCommand& req) -> void {
        Command::dispatch(e.world(), req.sender, req.text);
        e.destruct();
    });
    world.observer<const RequestViewInteraction>("script::view_event").event(flecs::OnSet).each([](flecs::entity e, const RequestViewInteraction& req) -> void {
        Command::view_event(e.world(), req.sender, req.handler, req.values);
        e.destruct();
    });
    world.observer<const RequestPlayerJoin>("script::player_join").event(flecs::OnSet).each([](flecs::entity e, const RequestPlayerJoin& req) -> void {
        player_join(e.world(), req.peer, req.username);
        e.destruct();
    });
    world.observer<const RequestPlayerLeave>("script::player_leave").event(flecs::OnSet).each([](flecs::entity e, const RequestPlayerLeave& req) -> void {
        flecs::world world = e.world();
        emit(ScriptState::of(world), "player_leave", [&](lua_State* lua) -> void {
            LuaRef ev = luabridge::newTable(lua);
            ev["player"] = ScriptPlayer{.peer = req.peer};
            ev.push(lua);
        });
        ScriptState::of(world).usernames.erase(req.peer.id());
        e.destruct();
    });
    world.observer().with<RequestHost>().event(flecs::OnSet).each([](flecs::entity e) -> void {
        flecs::world world = e.world();
        Mods::load(world);
        Reflect::refresh_components(world);
        generate_types(world);
        wire_component_observers(world);
        world.set<CommandBook>({.commands = Command::command_list(world)});
        world.entity().add<RequestReload>();
    });
    world.observer().with<RequestQuit>().event(flecs::OnAdd).each([](flecs::entity e) -> void { unload_scripts(e.world()); });

    world.observer<const RequestTileUpdate>("script::tile_update").event(flecs::OnSet).each([](flecs::entity e, const RequestTileUpdate& req) -> void {
        ScriptState& state = ScriptState::of(e.world());
        lua_State* lua = state.lua;
        for (const TileUpdateEntry& entry : req.entries) {
            auto it = state.tile_rules.find(entry.id);
            if (it == state.tile_rules.end() || !it->second.isFunction()) {
                continue;
            }
            it->second.push(lua);
            lua_pushnumber(lua, entry.tx);
            lua_pushnumber(lua, entry.ty);
            int status = 0;
            {
                BudgetGuard guard(lua);
                status = lua_pcall(lua, 2, 0, 0);
            }
            if (status != 0) {
                SDL_Log("[lua] tile_update error: %s", lua_tostring(lua, -1));
                lua_pop(lua, 1);
            }
        }
        e.destruct();
    });

    world.observer<const RequestGenerateChunk>("script::generate").event(flecs::OnSet).each([](flecs::entity e, const RequestGenerateChunk& req) -> void {
        ScriptState& state = ScriptState::of(e.world());
        if (state.generator && state.generator->isFunction()) {
            lua_State* lua = state.lua;
            state.generator->push(lua);
            lua_pushnumber(lua, req.cx);
            lua_pushnumber(lua, req.cy);
            int status = 0;
            {
                BudgetGuard guard(lua);
                status = lua_pcall(lua, 2, 0, 0);
            }
            if (status != 0) {
                SDL_Log("[lua] generator error: %s", lua_tostring(lua, -1));
                lua_pop(lua, 1);
            }
        }
        e.destruct();
    });
}
