#include "command.h"

#include <algorithm>
#include <cstdint>

#include <SDL3/SDL.h>

static void chat_reply(flecs::world world, flecs::entity peer, const std::string& line) { world.entity().set(RequestReply{.peer = peer, .line = line}); }

static auto sender_owner(const CommandSender& sender) -> uint64_t { return (sender.peer && sender.peer.is_alive()) ? static_cast<uint64_t>(sender.peer.id()) : 0; }

static void clear_view_handlers(ScriptState& state, uint64_t owner, const std::string& view) {
    for (auto it = state.view_owner.begin(); it != state.view_owner.end();) {
        if (it->second.owner == owner && it->second.view == view) {
            it = state.view_owner.erase(it);
        } else {
            ++it;
        }
    }
}

static void read_value_or_bind(const LuaRef& widget, const char* key, std::string& bind, float& number) {
    LuaRef value = widget[key];
    if (value.isTable()) {
        LuaRef path = value["__bind"];
        if (path.isString()) {
            bind = path.unsafe_cast<std::string>();
        }
    } else if (value.isNumber()) {
        number = static_cast<float>(value.unsafe_cast<double>());
    }
}

static auto build_view_widget(ScriptState& state, const LuaRef& widget, uint64_t owner, const std::string& view) -> ViewWidget {
    ViewWidget out;
    std::string kind = Lua::ref_string(widget, "kind", "label");
    if (kind == "panel") {
        out.kind = ViewKind::Panel;
        out.layout = Lua::ref_string(widget, "layout", "column") == "row" ? ViewLayout::Row : ViewLayout::Column;
        out.text = Lua::ref_string(widget, "title", "");
        LuaRef background = widget["background"];
        if (background.isBool() && !background.unsafe_cast<bool>()) {
            out.bg_a = 0;
        } else if (background.isTable()) {
            out.bg_r = static_cast<uint8_t>(Lua::ref_number(background, "r", 35.0));
            out.bg_g = static_cast<uint8_t>(Lua::ref_number(background, "g", 35.0));
            out.bg_b = static_cast<uint8_t>(Lua::ref_number(background, "b", 40.0));
            out.bg_a = static_cast<uint8_t>(Lua::ref_number(background, "a", 255.0));
        }
        int count = widget.length();
        for (int i = 1; i <= count; ++i) {
            LuaRef child = widget[i];
            if (!child.isTable()) {
                continue;
            }
            if (Lua::ref_string(child, "kind", "") == "each") {
                LuaRef items = child["items"];
                LuaRef builder = child["builder"];
                if (items.isTable() && builder.isFunction()) {
                    for (auto&& entry : luabridge::pairs(items)) {
                        luabridge::LuaResult result = [&]() -> luabridge::LuaResult {
                            BudgetGuard guard(state.lua);
                            return builder(entry.second);
                        }();
                        if (result && result.size() > 0 && result[0].isTable()) {
                            out.children.push_back(build_view_widget(state, result[0], owner, view));
                        }
                    }
                }
            } else {
                out.children.push_back(build_view_widget(state, child, owner, view));
            }
        }
    } else if (kind == "button") {
        out.kind = ViewKind::Button;
        out.text = Lua::ref_string(widget, "text", "");
        LuaRef fn = widget["on_click"];
        if (fn.isFunction()) {
            uint32_t hid = ++state.view_next;
            state.view_owner.insert_or_assign(hid, ViewHandler{.owner = owner, .view = view, .fn = fn});
            out.handler = hid;
        }
    } else if (kind == "bar") {
        out.kind = ViewKind::Bar;
        read_value_or_bind(widget, "value", out.bind, out.number);
        read_value_or_bind(widget, "max", out.bind_max, out.number_max);
        LuaRef color = widget["color"];
        if (color.isTable()) {
            out.color_r = static_cast<uint8_t>(Lua::ref_number(color, "r", 80.0));
            out.color_g = static_cast<uint8_t>(Lua::ref_number(color, "g", 200.0));
            out.color_b = static_cast<uint8_t>(Lua::ref_number(color, "b", 120.0));
        }
    } else if (kind == "input") {
        out.kind = ViewKind::Input;
        out.field = Lua::ref_string(widget, "field", "");
        out.text = Lua::ref_string(widget, "placeholder", "");
    } else if (kind == "spacer") {
        out.kind = ViewKind::Spacer;
        out.number = static_cast<float>(Lua::ref_number(widget, "value", 8.0));
    } else if (kind == "separator") {
        out.kind = ViewKind::Separator;
    } else if (kind == "slider") {
        out.kind = ViewKind::Slider;
        out.field = Lua::ref_string(widget, "field", "");
        out.number = static_cast<float>(Lua::ref_number(widget, "min", 0.0));
        out.number_max = static_cast<float>(Lua::ref_number(widget, "max", 100.0));
    } else if (kind == "toggle") {
        out.kind = ViewKind::Toggle;
        out.field = Lua::ref_string(widget, "field", "");
        out.text = Lua::ref_string(widget, "label", "");
    } else {
        out.kind = ViewKind::Label;
        LuaRef value = widget["value"];
        if (value.isTable()) {
            LuaRef path = value["__bind"];
            if (path.isString()) {
                out.bind = path.unsafe_cast<std::string>();
            }
        } else if (value.isString()) {
            out.text = value.unsafe_cast<std::string>();
        }
    }
    return out;
}

static void script_open_view(flecs::world world, const CommandSender& sender, const std::string& id, const LuaRef& tree) {
    ScriptState& state = ScriptState::of(world);
    uint64_t owner = sender_owner(sender);
    clear_view_handlers(state, owner, id);
    RequestView req;
    req.peer = sender.peer;
    req.id = id;
    req.placement = Lua::ref_string(tree, "placement", "center") == "hud" ? ViewPlacement::Hud : ViewPlacement::Center;
    req.root = build_view_widget(state, tree, owner, id);
    world.entity().set(std::move(req));
}

static void script_close_view(flecs::world world, const CommandSender& sender, const std::string& id) {
    clear_view_handlers(ScriptState::of(world), sender_owner(sender), id);
    world.entity().set(RequestView{.peer = sender.peer, .id = id, .close = true});
}

void ScriptContext::open_view(const std::string& id, const LuaRef& tree) { script_open_view(world, sender, id, tree); }
void ScriptContext::close_view(const std::string& id) { script_close_view(world, sender, id); }

void Command::open_view(flecs::world world, flecs::entity peer, const std::string& id, const LuaRef& tree) { script_open_view(world, {.peer = peer}, id, tree); }
void Command::close_view(flecs::world world, flecs::entity peer, const std::string& id) { script_close_view(world, {.peer = peer}, id); }

auto Command::context(flecs::world world, const CommandSender& sender) -> ScriptContext {
    lua_State* lua = ScriptState::of(world).lua;
    ScriptContext ctx(lua);
    ctx.world = world;
    ctx.sender = sender;
    if (sender.peer && sender.peer.is_alive()) {
        ctx.player = LuaRef(lua, ScriptPlayer{.peer = sender.peer});
    }
    return ctx;
}

static auto resolve_enum_values(ScriptState& state, const std::string& type) -> std::vector<std::string> {
    auto alias = state.enum_aliases.find(type);
    return alias != state.enum_aliases.end() ? alias->second : std::vector<std::string>{};
}

static auto read_explicit_args(ScriptState& state, const LuaRef& args) -> std::vector<CommandArgument> {
    std::vector<CommandArgument> out;
    int count = args.length();
    for (int i = 1; i <= count; ++i) {
        LuaRef arg = args[i];
        if (arg.isTable()) {
            CommandArgument entry;
            entry.name = Lua::ref_string(arg, "name", std::to_string(i));
            entry.type = Lua::ref_string(arg, "type", "string");
            entry.optional = Lua::ref_bool(arg, "optional", false);
            entry.values = resolve_enum_values(state, entry.type);
            out.push_back(std::move(entry));
        }
    }
    return out;
}

static auto effective_args(ScriptState& state, const LuaRef& node, const std::string& key) -> std::vector<CommandArgument> {
    LuaRef args = node["args"];
    if (args.isTable()) {
        return read_explicit_args(state, args);
    }
    auto it = state.inferred.find(key);
    if (it == state.inferred.end()) {
        return {};
    }
    std::vector<CommandArgument> out = it->second;
    for (auto& entry : out) {
        if (entry.values.empty()) {
            entry.values = resolve_enum_values(state, entry.type);
        }
    }
    return out;
}

static auto build_command_info(ScriptState& state, const std::string& name, const LuaRef& spec, const std::string& key) -> CommandInfo {
    CommandInfo info;
    info.name = name;
    info.description = Lua::ref_string(spec, "description", "");
    info.arguments = effective_args(state, spec, key);
    LuaRef subs = spec["subcommands"];
    if (subs.isTable()) {
        for (auto&& entry : luabridge::pairs(subs)) {
            if (entry.first.isString() && entry.second.isTable()) {
                std::string sub = entry.first.unsafe_cast<std::string>();
                info.subcommands.push_back(build_command_info(state, sub, entry.second, key + "." + sub));
            }
        }
        std::sort(info.subcommands.begin(), info.subcommands.end(), [](const CommandInfo& a, const CommandInfo& b) -> bool { return a.name < b.name; });
    }
    return info;
}

auto Command::command_list(flecs::world world) -> std::vector<CommandInfo> {
    std::vector<CommandInfo> out;
    ScriptState& state = ScriptState::of(world);
    out.reserve(state.commands.size());
    for (const auto& [name, spec] : state.commands) {
        out.push_back(build_command_info(state, name, spec, name));
    }
    std::sort(out.begin(), out.end(), [](const CommandInfo& a, const CommandInfo& b) -> bool { return a.name < b.name; });
    return out;
}

static auto is_numeric_type(const std::string& type) -> bool { return type == "number" || type == "integer"; }

static auto is_number_token(const std::string& token, bool integer) -> bool {
    if (token.empty()) {
        return false;
    }
    char* end = nullptr;
    if (integer) {
        (void)std::strtoll(token.c_str(), &end, 10);
    } else {
        (void)std::strtod(token.c_str(), &end);
    }
    return end == token.c_str() + token.size();
}

static void tokenize(const std::string& text, std::vector<std::string>& tokens) {
    for (size_t i = 1; i < text.size();) {
        while (i < text.size() && text[i] == ' ') {
            ++i;
        }
        size_t begin = i;
        while (i < text.size() && text[i] != ' ') {
            ++i;
        }
        if (i > begin) {
            tokens.push_back(text.substr(begin, i - begin));
        }
    }
}

auto Command::dispatch(flecs::world world, const CommandSender& sender, const std::string& text) -> bool {
    ScriptState& state = ScriptState::of(world);

    std::vector<std::string> tokens;
    tokenize(text, tokens);
    if (tokens.empty()) {
        return true;
    }

    auto root = state.commands.find(tokens[0]);
    if (root == state.commands.end()) {
        chat_reply(world, sender.peer, "§cUnknown command: " + tokens[0]);
        return true;
    }

    LuaRef node = root->second;
    std::vector<std::string> path{tokens[0]};
    bool need_admin = Lua::ref_string(node, "permission", "") == "admin";
    size_t ti = 1;
    while (ti < tokens.size()) {
        LuaRef subs = node["subcommands"];
        if (!subs.isTable()) {
            break;
        }
        LuaRef sub = subs[tokens[ti]];
        if (!sub.isTable()) {
            break;
        }
        node = sub;
        path.push_back(tokens[ti]);
        need_admin = need_admin || Lua::ref_string(node, "permission", "") == "admin";
        ++ti;
    }

    std::string full = "/" + path[0];
    for (size_t i = 1; i < path.size(); ++i) {
        full += " " + path[i];
    }

    if (need_admin && !sender.admin) {
        chat_reply(world, sender.peer, "§cYou don't have permission to use that");
        return true;
    }

    LuaRef run = node["run"];
    if (!run.isFunction()) {
        LuaRef subs = node["subcommands"];
        if (subs.isTable()) {
            std::vector<std::string> names;
            for (auto&& entry : luabridge::pairs(subs)) {
                if (entry.first.isString()) {
                    names.push_back(entry.first.unsafe_cast<std::string>());
                }
            }
            std::sort(names.begin(), names.end());
            std::string usage;
            for (size_t i = 0; i < names.size(); ++i) {
                usage += (i == 0 ? "" : "|") + names[i];
            }
            chat_reply(world, sender.peer, "§eUsage: §f" + full + " <" + usage + ">");
        } else {
            chat_reply(world, sender.peer, "§cThat command has no handler");
        }
        return true;
    }

    std::string key = path[0];
    for (size_t i = 1; i < path.size(); ++i) {
        key += "." + path[i];
    }
    std::vector<CommandArgument> specs = effective_args(state, node, key);
    size_t declared = specs.size();
    size_t supplied = tokens.size() - ti;

    size_t required = 0;
    for (size_t a = 0; a < declared; ++a) {
        if (!specs[a].optional) {
            required = a + 1;
        }
    }

    if (supplied < required) {
        std::string usage = "§eUsage: §f" + full;
        for (const auto& spec : specs) {
            std::string inner = spec.name + ": " + spec.type;
            usage += spec.optional ? " [" + inner + "]" : " <" + inner + ">";
        }
        chat_reply(world, sender.peer, usage);
        return true;
    }

    for (size_t a = 0; a < declared && a < supplied; ++a) {
        const std::string& type = specs[a].type;
        const std::string& token = tokens[ti + a];
        if (is_numeric_type(type)) {
            if (!is_number_token(token, type == "integer")) {
                chat_reply(world, sender.peer, "§cArgument §f<" + specs[a].name + ">§c must be a number");
                return true;
            }
            continue;
        }
        const std::vector<std::string>& values = specs[a].values;
        if (!values.empty() && std::find(values.begin(), values.end(), token) == values.end()) {
            std::string options;
            for (size_t i = 0; i < values.size(); ++i) {
                options += (i == 0 ? "" : "§7|§f") + values[i];
            }
            chat_reply(world, sender.peer, "§cArgument §f<" + specs[a].name + ">§c must be one of: §f" + options);
            return true;
        }
    }

    lua_State* lua = state.lua;
    run.push(lua);
    Lua::push(lua, Command::context(world, sender));
    int argc = 1;
    for (size_t a = 0; a < declared; ++a) {
        if (a >= supplied) {
            lua_pushnil(lua);
        } else {
            const std::string& token = tokens[ti + a];
            if (specs[a].type == "integer") {
                lua_pushinteger(lua, static_cast<int>(std::strtoll(token.c_str(), nullptr, 10)));
            } else if (is_numeric_type(specs[a].type)) {
                lua_pushnumber(lua, std::strtod(token.c_str(), nullptr));
            } else {
                lua_pushstring(lua, token.c_str());
            }
        }
        ++argc;
    }

    int status = 0;
    {
        BudgetGuard guard(lua);
        status = lua_pcall(lua, argc, 0, 0);
    }
    if (status != 0) {
        SDL_Log("[lua] command '%s' error: %s", full.c_str(), lua_tostring(lua, -1));
        lua_pop(lua, 1);
        chat_reply(world, sender.peer, "§cThe command failed");
    }
    return true;
}

void Command::view_event(flecs::world world, const CommandSender& sender, uint32_t handler, const std::vector<std::pair<std::string, std::string>>& values) {
    ScriptState& state = ScriptState::of(world);
    auto it = state.view_owner.find(handler);
    if (it == state.view_owner.end() || it->second.owner != sender_owner(sender) || !it->second.fn.isFunction()) {
        return;
    }
    lua_State* lua = state.lua;
    ScriptContext ctx = Command::context(world, sender);
    ctx.form = luabridge::newTable(lua);
    for (const auto& [key, value] : values) {
        ctx.form[key] = value;
    }
    it->second.fn.push(lua);
    Lua::push(lua, ctx);
    int status = 0;
    {
        BudgetGuard guard(lua);
        status = lua_pcall(lua, 1, 0, 0);
    }
    if (status != 0) {
        SDL_Log("[lua] view handler error: %s", lua_tostring(lua, -1));
        lua_pop(lua, 1);
    }
}
