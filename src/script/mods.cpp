#include "mods.h"

#include <luacode.h>

#include <Luau/Ast.h>
#include <Luau/Lexer.h>
#include <Luau/Parser.h>

#include <glaze/glaze.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <SDL3/SDL.h>

#include "reflect.h"

namespace fs = std::filesystem;

struct ModManifest {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> depends;
    bool enabled = true;
};

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
    std::vector<fs::path> ordered = resolve_order(world);
    auto run_stage = [&](const char* stem) -> void {
        for (const auto& dir : ordered) {
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

auto Mods::resolve_order(flecs::world world) -> std::vector<fs::path> {
    struct Entry {
        fs::path dir;
        ModInfo info;
    };
    std::vector<Entry> entries;
    std::unordered_set<std::string> disabled;
    for (const auto& item : fs::directory_iterator("mods")) {
        if (!item.is_directory()) {
            continue;
        }
        Entry entry;
        entry.dir = item.path();
        entry.info.id = item.path().filename().string();
        entry.info.name = entry.info.id;
        fs::path manifest_path = item.path() / "mod.json";
        if (fs::exists(manifest_path)) {
            ModManifest manifest;
            std::string buffer;
            auto error = glz::read_file_json<glz::opts{.error_on_unknown_keys = false}>(manifest, manifest_path.string(), buffer);
            if (error) {
                SDL_Log("[script] mod '%s': invalid mod.json (%s), using defaults", entry.info.id.c_str(), glz::format_error(error, buffer).c_str());
            } else {
                if (!manifest.name.empty()) {
                    entry.info.name = manifest.name;
                }
                entry.info.version = manifest.version;
                entry.info.description = manifest.description;
                entry.info.depends = manifest.depends;
                if (!manifest.enabled) {
                    SDL_Log("[script] mod '%s' is disabled via mod.json", entry.info.id.c_str());
                    disabled.insert(entry.info.id);
                    disabled.insert(entry.info.name);
                    continue;
                }
            }
        }
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) -> bool { return a.dir < b.dir; });

    std::unordered_map<std::string, size_t> by_id;
    for (size_t i = 0; i < entries.size(); ++i) {
        by_id[entries[i].info.id] = i;
        by_id[entries[i].info.name] = i;
    }
    std::vector<int> missing_dep(entries.size(), 0);
    std::vector<std::vector<size_t>> dependents(entries.size());
    std::vector<int> pending(entries.size(), 0);
    for (size_t i = 0; i < entries.size(); ++i) {
        for (const std::string& dep : entries[i].info.depends) {
            auto it = by_id.find(dep);
            if (it == by_id.end()) {
                const char* why = disabled.contains(dep) ? "disabled" : "not installed";
                SDL_Log("[script] mod '%s' depends on '%s' which is %s; skipping '%s'", entries[i].info.id.c_str(), dep.c_str(), why, entries[i].info.id.c_str());
                missing_dep[i] = 1;
                continue;
            }
            dependents[it->second].push_back(i);
            ++pending[i];
        }
    }

    std::vector<size_t> queue;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (pending[i] == 0) {
            queue.push_back(i);
        }
    }
    std::vector<size_t> sorted;
    for (size_t head = 0; head < queue.size(); ++head) {
        size_t i = queue[head];
        sorted.push_back(i);
        for (size_t d : dependents[i]) {
            if (--pending[d] == 0) {
                queue.push_back(d);
            }
        }
    }
    if (sorted.size() < entries.size()) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (pending[i] > 0) {
                SDL_Log("[script] mod '%s' is part of a dependency cycle; loading in directory order", entries[i].info.id.c_str());
                sorted.push_back(i);
            }
        }
    }

    ModBook book;
    std::vector<fs::path> ordered;
    for (size_t i : sorted) {
        if (missing_dep[i] != 0) {
            continue;
        }
        bool dropped_parent = false;
        for (const std::string& dep : entries[i].info.depends) {
            auto it = by_id.find(dep);
            if (it != by_id.end() && missing_dep[it->second] != 0) {
                SDL_Log("[script] mod '%s' depends on skipped mod '%s'; skipping too", entries[i].info.id.c_str(), dep.c_str());
                missing_dep[i] = 1;
                dropped_parent = true;
                break;
            }
        }
        if (dropped_parent) {
            continue;
        }
        ordered.push_back(entries[i].dir);
        book.mods.push_back(entries[i].info);
    }
    if (!book.mods.empty()) {
        std::string summary;
        for (const auto& info : book.mods) {
            if (!summary.empty()) {
                summary += ", ";
            }
            summary += info.id;
            if (!info.version.empty()) {
                summary += " " + info.version;
            }
        }
        SDL_Log("[script] mods loaded in order: %s", summary.c_str());
    }
    world.set<ModBook>(std::move(book));
    return ordered;
}
