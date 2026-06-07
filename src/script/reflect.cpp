#include "reflect.h"

#include <cmath>

#include "component/network.h"

static auto read_field(ecs_primitive_kind_t kind, const void* ptr) -> double {
    switch (kind) {
        case EcsBool:
            return *static_cast<const bool*>(ptr) ? 1.0 : 0.0;
        case EcsChar:
        case EcsByte:
        case EcsU8:
            return *static_cast<const uint8_t*>(ptr);
        case EcsI8:
            return *static_cast<const int8_t*>(ptr);
        case EcsU16:
            return *static_cast<const uint16_t*>(ptr);
        case EcsI16:
            return *static_cast<const int16_t*>(ptr);
        case EcsU32:
            return *static_cast<const uint32_t*>(ptr);
        case EcsI32:
            return *static_cast<const int32_t*>(ptr);
        case EcsU64:
            return static_cast<double>(*static_cast<const uint64_t*>(ptr));
        case EcsI64:
            return static_cast<double>(*static_cast<const int64_t*>(ptr));
        case EcsF32:
            return *static_cast<const float*>(ptr);
        case EcsF64:
            return *static_cast<const double*>(ptr);
        default:
            return 0.0;
    }
}

static void write_field(ecs_primitive_kind_t kind, void* ptr, double value) {
    switch (kind) {
        case EcsBool:
            *static_cast<bool*>(ptr) = value != 0.0;
            break;
        case EcsChar:
        case EcsByte:
        case EcsU8:
            *static_cast<uint8_t*>(ptr) = static_cast<uint8_t>(std::llround(value));
            break;
        case EcsI8:
            *static_cast<int8_t*>(ptr) = static_cast<int8_t>(std::llround(value));
            break;
        case EcsU16:
            *static_cast<uint16_t*>(ptr) = static_cast<uint16_t>(std::llround(value));
            break;
        case EcsI16:
            *static_cast<int16_t*>(ptr) = static_cast<int16_t>(std::llround(value));
            break;
        case EcsU32:
            *static_cast<uint32_t*>(ptr) = static_cast<uint32_t>(std::llround(value));
            break;
        case EcsI32:
            *static_cast<int32_t*>(ptr) = static_cast<int32_t>(std::llround(value));
            break;
        case EcsU64:
            *static_cast<uint64_t*>(ptr) = static_cast<uint64_t>(std::llround(value));
            break;
        case EcsI64:
            *static_cast<int64_t*>(ptr) = static_cast<int64_t>(std::llround(value));
            break;
        case EcsF32:
            *static_cast<float*>(ptr) = static_cast<float>(value);
            break;
        case EcsF64:
            *static_cast<double*>(ptr) = value;
            break;
        default:
            break;
    }
}

auto Reflect::component_entity(flecs::world world, const std::string& name) -> flecs::entity_t {
    auto& components = ScriptState::of(world).components;
    auto it = components.find(name);
    return it != components.end() ? it->second : 0;
}

auto Reflect::component_ref_name(const LuaRef& ref) -> std::string {
    if (ref.isString()) {
        return ref.unsafe_cast<std::string>();
    }
    if (ref.isTable()) {
        LuaRef tag = ref["__component"];
        if (tag.isString()) {
            return tag.unsafe_cast<std::string>();
        }
    }
    return {};
}

auto Reflect::is_known_component(flecs::world world, const std::string& name) -> bool { return ScriptState::of(world).components.contains(name); }

auto Reflect::entity_has_component(flecs::entity entity, const std::string& component) -> bool {
    flecs::entity_t comp = component_entity(entity.world(), component);
    return comp != 0 && entity.is_alive() && ecs_has_id(entity.world().c_ptr(), entity.id(), comp);
}

auto Reflect::component_to_ref(lua_State* state, flecs::entity entity, const std::string& name) -> LuaRef {
    flecs::world world = entity.world();
    flecs::entity_t comp = component_entity(world, name);
    if (!entity.is_alive() || comp == 0 || !ecs_has_id(world.c_ptr(), entity.id(), comp)) {
        return {state};
    }
    const void* base = ecs_get_id(world.c_ptr(), entity.id(), comp);
    const auto* layout = ecs_get(world.c_ptr(), comp, EcsStruct);
    if (base == nullptr || layout == nullptr) {
        return {state};
    }
    LuaRef table = luabridge::newTable(state);
    int32_t count = ecs_vec_count(&layout->members);
    for (int32_t i = 0; i < count; ++i) {
        auto* member = ecs_vec_get_t(&layout->members, ecs_member_t, i);
        const auto* primitive = ecs_get(world.c_ptr(), member->type, EcsPrimitive);
        if (primitive != nullptr) {
            const auto* field = static_cast<const uint8_t*>(base) + member->offset;
            if (primitive->kind == EcsString) {
                const char* str = *reinterpret_cast<const char* const*>(field);
                table[member->name] = str != nullptr ? std::string(str) : std::string();
            } else {
                table[member->name] = read_field(primitive->kind, field);
            }
        }
    }
    return table;
}

void Reflect::set_component_from_ref(flecs::entity entity, const std::string& name, const LuaRef& value) {
    flecs::world world = entity.world();
    flecs::entity_t comp = component_entity(world, name);
    if (!entity.is_alive() || comp == 0 || !value.isTable()) {
        return;
    }
    const auto* layout = ecs_get(world.c_ptr(), comp, EcsStruct);
    const ecs_type_info_t* info = ecs_get_type_info(world.c_ptr(), comp);
    if (layout == nullptr || info == nullptr) {
        return;
    }
    void* base = ecs_ensure_id(world.c_ptr(), entity.id(), comp, static_cast<size_t>(info->size));
    int32_t count = ecs_vec_count(&layout->members);
    for (int32_t i = 0; i < count; ++i) {
        auto* member = ecs_vec_get_t(&layout->members, ecs_member_t, i);
        const auto* primitive = ecs_get(world.c_ptr(), member->type, EcsPrimitive);
        if (primitive == nullptr) {
            continue;
        }
        LuaRef field = value[member->name];
        void* ptr = static_cast<uint8_t*>(base) + member->offset;
        if (primitive->kind == EcsString) {
            if (field.isString()) {
                char** slot = reinterpret_cast<char**>(ptr);
                ecs_os_free(*slot);
                *slot = ecs_os_strdup(field.unsafe_cast<std::string>().c_str());
            }
        } else if (field.isNumber()) {
            write_field(primitive->kind, ptr, field.unsafe_cast<double>());
        } else if (field.isBool()) {
            write_field(primitive->kind, ptr, field.unsafe_cast<bool>() ? 1.0 : 0.0);
        }
    }
    ecs_modified_id(world.c_ptr(), entity.id(), comp);
}


static auto proxy_index(lua_State* lua) -> int {
    const char* component = lua_tostring(lua, lua_upvalueindex(1));
    const char* field = lua_tostring(lua, 2);
    std::string path = std::string(component != nullptr ? component : "") + "." + (field != nullptr ? field : "");
    lua_newtable(lua);
    lua_pushstring(lua, path.c_str());
    lua_setfield(lua, -2, "__bind");
    return 1;
}

static void create_binding_proxy(lua_State* lua, const std::string& name) {
    lua_newtable(lua);
    lua_pushstring(lua, name.c_str());
    lua_setfield(lua, -2, "__component");
    lua_newtable(lua);
    lua_pushstring(lua, name.c_str());
    lua_pushcclosure(lua, proxy_index, "__index", 1);
    lua_setfield(lua, -2, "__index");
    lua_setmetatable(lua, -2);
    lua_setglobal(lua, name.c_str());
}

static auto type_primitive(const std::string& type) -> ecs_entity_t {
    if (type == "f64" || type == "double" || type == "number") {
        return ecs_id(ecs_f64_t);
    }
    if (type == "i8") {
        return ecs_id(ecs_i8_t);
    }
    if (type == "u8" || type == "byte") {
        return ecs_id(ecs_u8_t);
    }
    if (type == "i16") {
        return ecs_id(ecs_i16_t);
    }
    if (type == "u16") {
        return ecs_id(ecs_u16_t);
    }
    if (type == "i32" || type == "int") {
        return ecs_id(ecs_i32_t);
    }
    if (type == "u32") {
        return ecs_id(ecs_u32_t);
    }
    if (type == "i64") {
        return ecs_id(ecs_i64_t);
    }
    if (type == "u64") {
        return ecs_id(ecs_u64_t);
    }
    if (type == "bool") {
        return ecs_id(ecs_bool_t);
    }
    if (type == "string") {
        return ecs_id(ecs_string_t);
    }
    return ecs_id(ecs_f32_t);
}

static void forget_observers(ScriptState& state, const std::string& name) {
    state.observed.erase("set:" + name);
    state.observed.erase("add:" + name);
    state.observed.erase("remove:" + name);
}

void Reflect::remove_component(flecs::world world, const std::string& name) {
    ScriptState& state = ScriptState::of(world);
    auto it = state.components.find(name);
    if (it != state.components.end()) {
        flecs::entity(world, it->second).destruct();
        state.components.erase(it);
    }
    state.author_components.erase(name);
    state.component_defs.erase(name);
    forget_observers(state, name);
    lua_pushnil(state.lua);
    lua_setglobal(state.lua, name.c_str());
}

void Reflect::define_component(flecs::world world, const ComponentDef& def) {
    ScriptState& state = ScriptState::of(world);
    if (def.name.empty() || def.fields.size() > ECS_MEMBER_DESC_CACHE_SIZE) {
        return;
    }
    state.declared_this_load.insert(def.name);
    auto previous = state.component_defs.find(def.name);
    if (previous != state.component_defs.end()) {
        if (previous->second == def) {
            return;
        }
        auto live = state.components.find(def.name);
        if (live != state.components.end()) {
            flecs::entity(world, live->second).destruct();
            state.components.erase(live);
        }
        forget_observers(state, def.name);
    }
    ecs_world_t* raw = world.c_ptr();
    ecs_entity_desc_t entity_desc = {};
    entity_desc.name = def.name.c_str();
    ecs_entity_t comp = ecs_entity_init(raw, &entity_desc);

    if (!def.is_tag && !def.fields.empty()) {
        ecs_struct_desc_t struct_desc = {};
        struct_desc.entity = comp;
        std::vector<std::string> names(def.fields.size());
        for (size_t i = 0; i < def.fields.size(); ++i) {
            names[i] = def.fields[i].name;
            struct_desc.members[i].name = names[i].c_str();
            struct_desc.members[i].type = type_primitive(def.fields[i].type);
        }
        ecs_struct_init(raw, &struct_desc);
    }
    if (def.replicated) {
        flecs::entity(world, comp).add<Networked>();
    }
    state.components[def.name] = comp;
    state.author_components.insert(def.name);
    state.component_defs[def.name] = def;
    create_binding_proxy(state.lua, def.name);
}

void Reflect::register_component(flecs::world world, const std::string& name, flecs::entity_t comp) {
    ScriptState& state = ScriptState::of(world);
    state.components.insert_or_assign(name, comp);
    create_binding_proxy(state.lua, name);
}

static auto is_internal_component(const std::string& name) -> bool {
    static const std::unordered_set<std::string> internal = {
        "B2Body", "ClientQueries", "ServerQueries", "ConnectionStatus", "History", "Interest",
        "Interpolation", "Local", "NetworkConfig", "NetworkId", "NetworkRegistry", "NetworkTarget", "Networked",
        "Peer", "PhysicsConfig", "PhysicsEngine", "PhysicsEvents", "Predicted", "Quantize", "Remote", "Replication",
        "ScriptState", "ServerClock", "Settings", "SimulationClock",
    };
    return internal.contains(name);
}

void Reflect::refresh_components(flecs::world world) {
    ScriptState& state = ScriptState::of(world);
    world.query_builder().with<flecs::Component>().build().each([&](flecs::entity comp) -> void {
        std::string name = comp.name().c_str();
        if (name.empty()) {
            return;
        }
        std::string path = comp.path().c_str();
        if (path.find("flecs") != std::string::npos) {
            return;
        }
        if (comp.has(flecs::Module) || is_internal_component(name)) {
            return;
        }
        if (name.rfind("Request", 0) == 0 || name.rfind("Response", 0) == 0) {
            return;
        }
        state.components.insert({name, comp.id()});
        create_binding_proxy(state.lua, name);
    });
}
