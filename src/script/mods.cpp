#include "mods.h"

#include <luacode.h>

#include <Luau/Ast.h>
#include <Luau/Lexer.h>
#include <Luau/Parser.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <SDL3/SDL.h>

#include "reflect.h"

namespace fs = std::filesystem;

static auto annotation_type(Luau::AstType* annotation, CommandArgument& info) -> void {
    if (annotation == nullptr) {
        info.type = "string";
        return;
    }
    if (auto* ref = annotation->as<Luau::AstTypeReference>()) {
        info.type = ref->name.value;
        return;
    }
    if (auto* uni = annotation->as<Luau::AstTypeUnion>()) {
        for (Luau::AstType* member : uni->types) {
            if (member->is<Luau::AstTypeOptional>()) {
                info.optional = true;
            } else if (auto* ref = member->as<Luau::AstTypeReference>()) {
                if (std::strcmp(ref->name.value, "nil") == 0) {
                    info.optional = true;
                } else {
                    info.type = ref->name.value;
                }
            } else if (auto* str = member->as<Luau::AstTypeSingletonString>()) {
                info.type = "enum";
                info.values.emplace_back(str->value.data, str->value.size);
            }
        }
        std::sort(info.values.begin(), info.values.end());
    }
}

static auto extract_args(Luau::AstExprFunction* fn) -> std::vector<CommandArgument> {
    std::vector<CommandArgument> out;
    for (size_t i = 1; i < fn->args.size; ++i) {
        Luau::AstLocal* local = fn->args.data[i];
        CommandArgument info;
        info.name = local->name.value;
        annotation_type(local->annotation, info);
        out.push_back(std::move(info));
    }
    return out;
}

static void infer_command(ScriptState& state, Luau::AstExprTable* spec, const std::string& key) {
    if (auto run = spec->getRecord("run")) {
        if (auto* fn = (*run)->as<Luau::AstExprFunction>()) {
            state.inferred[key] = extract_args(fn);
        }
    }
    if (auto subs = spec->getRecord("subcommands")) {
        if (auto* table = (*subs)->as<Luau::AstExprTable>()) {
            for (const Luau::AstExprTable::Item& item : table->items) {
                auto* subname = item.key != nullptr ? item.key->as<Luau::AstExprConstantString>() : nullptr;
                auto* subspec = item.value != nullptr ? item.value->as<Luau::AstExprTable>() : nullptr;
                if (subname != nullptr && subspec != nullptr) {
                    infer_command(state, subspec, key + "." + std::string(subname->value.data, subname->value.size));
                }
            }
        }
    }
}

static auto map_ast_type(Luau::AstType* type) -> std::string {
    if (auto* ref = type != nullptr ? type->as<Luau::AstTypeReference>() : nullptr) {
        if (std::strcmp(ref->name.value, "boolean") == 0) {
            return "bool";
        }
        if (std::strcmp(ref->name.value, "string") == 0) {
            return "string";
        }
    }
    return "f32";
}

static void create_proto_handle(lua_State* lua, const std::string& name) {
    (void)luabridge::push(lua, ScriptProto{.category = name});
    lua_setglobal(lua, name.c_str());
}

static auto event_name_from_type(const char* type) -> std::string {
    std::string base = type;
    if (base.rfind("Event", 0) == 0) {
        base = base.substr(5);
    }
    std::string out;
    for (size_t i = 0; i < base.size(); ++i) {
        char c = base[i];
        if (c >= 'A' && c <= 'Z') {
            if (i > 0) {
                out += '_';
            }
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += c;
        }
    }
    return out;
}

struct ModVisitor : Luau::AstVisitor {
    ScriptState& state;
    std::vector<ComponentDef>& components;
    explicit ModVisitor(ScriptState& target, std::vector<ComponentDef>& comps) : state(target), components(comps) {}

    auto visit(Luau::AstExprCall* call) -> bool override {
        auto* index = call->func->as<Luau::AstExprIndexName>();
        if (index != nullptr && std::strcmp(index->index.value, "register") == 0 && call->args.size >= 2) {
            auto* global = index->expr->as<Luau::AstExprGlobal>();
            auto* name = call->args.data[0]->as<Luau::AstExprConstantString>();
            auto* spec = call->args.data[1]->as<Luau::AstExprTable>();
            if (global != nullptr && std::strcmp(global->name.value, "command") == 0 && name != nullptr && spec != nullptr) {
                infer_command(state, spec, std::string(name->value.data, name->value.size));
            }
        }
        if (index != nullptr && std::strcmp(index->index.value, "on") == 0 && call->args.size == 1) {
            auto* global = index->expr->as<Luau::AstExprGlobal>();
            auto* fn = call->args.data[0]->as<Luau::AstExprFunction>();
            if (global != nullptr && std::strcmp(global->name.value, "events") == 0 && fn != nullptr && fn->args.size >= 1) {
                if (auto* ref = fn->args.data[0]->annotation != nullptr ? fn->args.data[0]->annotation->as<Luau::AstTypeReference>() : nullptr) {
                    state.inferred_events.push_back(event_name_from_type(ref->name.value));
                }
            }
        }
        return true;
    }

    auto visit(Luau::AstStatTypeAlias* node) -> bool override {
        std::string name = node->name.value;
        if (auto* ref = node->type->as<Luau::AstTypeReference>()) {
            const char* marker = ref->name.value;
            if (std::strcmp(marker, "Prototype") == 0) {
                create_proto_handle(state.lua, name);
                std::vector<std::string> keys;
                if (ref->parameters.size >= 1 && ref->parameters.data[0].type != nullptr) {
                    if (auto* table = ref->parameters.data[0].type->as<Luau::AstTypeTable>()) {
                        for (const Luau::AstTableProp& prop : table->props) {
                            keys.emplace_back(prop.name.value);
                        }
                    }
                }
                state.proto_defs[name] = keys;
                return true;
            }
            bool replicated = std::strcmp(marker, "Replicated") == 0 || std::strcmp(marker, "ReplicatedTag") == 0;
            bool plain = std::strcmp(marker, "Component") == 0 || std::strcmp(marker, "ComponentTag") == 0;
            bool is_tag = std::strcmp(marker, "ComponentTag") == 0 || std::strcmp(marker, "ReplicatedTag") == 0;
            if (replicated || plain) {
                ComponentDef def{.name = name, .replicated = replicated, .is_tag = is_tag};
                if (!is_tag && ref->parameters.size >= 1 && ref->parameters.data[0].type != nullptr) {
                    if (auto* table = ref->parameters.data[0].type->as<Luau::AstTypeTable>()) {
                        for (const Luau::AstTableProp& prop : table->props) {
                            def.fields.push_back({.name = prop.name.value, .type = map_ast_type(prop.type)});
                        }
                    }
                }
                components.push_back(std::move(def));
            }
        } else if (auto* uni = node->type->as<Luau::AstTypeUnion>()) {
            std::vector<std::string> values;
            for (Luau::AstType* member : uni->types) {
                if (auto* str = member->as<Luau::AstTypeSingletonString>()) {
                    values.emplace_back(str->value.data, str->value.size);
                }
            }
            if (!values.empty()) {
                std::sort(values.begin(), values.end());
                state.enum_aliases[name] = std::move(values);
            }
        }
        return true;
    }

    auto visit(Luau::AstStatLocal* node) -> bool override {
        size_t n = node->vars.size < node->values.size ? node->vars.size : node->values.size;
        for (size_t i = 0; i < n; ++i) {
            auto* call = node->values.data[i]->as<Luau::AstExprCall>();
            if (call == nullptr) {
                continue;
            }
            auto* global = call->func->as<Luau::AstExprGlobal>();
            if (global != nullptr && std::strcmp(global->name.value, "Tile") == 0) {
                state.tile_names.emplace_back(node->vars.data[i]->name.value);
            }
        }
        return true;
    }
};

static void analyze_source(flecs::world world, const std::string& source) {
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    Luau::ParseResult result = Luau::Parser::parse(source.data(), source.size(), names, allocator, Luau::ParseOptions());
    if (result.root == nullptr) {
        return;
    }
    std::vector<ComponentDef> components;
    ModVisitor visitor(ScriptState::of(world), components);
    result.root->visit(&visitor);
    for (const ComponentDef& def : components) {
        Reflect::define_component(world, def);
    }
}

auto Mods::require(flecs::world world, const std::string& path) -> LuaRef {
    ScriptState& state = ScriptState::of(world);
    lua_State* lua = state.lua;
    auto cached = state.modules.find(path);
    if (cached != state.modules.end()) {
        return cached->second;
    }
    fs::path file = fs::path("mods") / (path + ".luau");
    if (!fs::exists(file)) {
        fs::path alt = fs::path("mods") / (path + ".lua");
        if (!fs::exists(alt)) {
            SDL_Log("[script] require: module not found: %s", path.c_str());
            return LuaRef(lua);
        }
        file = alt;
    }
    std::ifstream stream(file, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    std::string source = buffer.str();
    analyze_source(world, source);
    std::string chunk = "=" + file.string();
    size_t length = 0;
    char* bytecode = luau_compile(source.data(), source.size(), nullptr, &length);
    int loaded = luau_load(lua, chunk.c_str(), bytecode, length, 0);
    free(bytecode);
    if (loaded != 0) {
        SDL_Log("[script] require load error %s: %s", path.c_str(), lua_tostring(lua, -1));
        lua_pop(lua, 1);
        return LuaRef(lua);
    }
    int status = 0;
    {
        BudgetGuard guard(lua);
        status = lua_pcall(lua, 0, 1, 0);
    }
    if (status != 0) {
        SDL_Log("[script] require run error %s: %s", path.c_str(), lua_tostring(lua, -1));
        lua_pop(lua, 1);
        return LuaRef(lua);
    }
    LuaRef result = LuaRef::fromStack(lua, -1);
    lua_pop(lua, 1);
    state.modules.insert_or_assign(path, result);
    return result;
}

void Mods::load(flecs::world world) {
    if (!fs::exists("mods")) {
        SDL_Log("[script] no mods/ directory");
        return;
    }
    lua_State* lua = ScriptState::of(world).lua;
    auto run_source = [&](const fs::path& path) -> void {
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();
        analyze_source(world, source);
        std::string chunk = "=" + path.string();
        size_t length = 0;
        char* bytecode = luau_compile(source.data(), source.size(), nullptr, &length);
        int loaded = luau_load(lua, chunk.c_str(), bytecode, length, 0);
        free(bytecode);
        if (loaded != 0) {
            SDL_Log("[script] load error %s: %s", path.string().c_str(), lua_tostring(lua, -1));
            lua_pop(lua, 1);
            return;
        }
        int status = 0;
        {
            BudgetGuard guard(lua);
            status = lua_pcall(lua, 0, 0, 0);
        }
        if (status != 0) {
            SDL_Log("[script] run error %s: %s", path.string().c_str(), lua_tostring(lua, -1));
            lua_pop(lua, 1);
            return;
        }
        SDL_Log("[script] loaded %s", path.string().c_str());
    };
    auto run_stage = [&](const char* stem) -> void {
        std::vector<fs::path> dirs;
        for (const auto& entry : fs::directory_iterator("mods")) {
            if (entry.is_directory()) {
                dirs.push_back(entry.path());
            }
        }
        std::sort(dirs.begin(), dirs.end());
        for (const auto& dir : dirs) {
            fs::path luau = dir / (std::string(stem) + ".luau");
            fs::path lua_path = dir / (std::string(stem) + ".lua");
            if (fs::exists(luau)) {
                run_source(luau);
            } else if (fs::exists(lua_path)) {
                run_source(lua_path);
            }
        }
    };
    run_stage("data");
    run_stage("runtime");
    run_stage("commands");
}
