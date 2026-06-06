#pragma once

#include "component/network.h"
#include "state.h"

struct ScriptContext {
    flecs::world world;
    CommandSender sender;
    LuaRef player;
    LuaRef form;

    explicit ScriptContext(lua_State* state) : player(state), form(state) {}

    auto name() const -> std::string { return sender.name; }
    void reply(const std::string& message) { world.entity().set(RequestReply{.peer = sender.peer, .line = message}); }
    void kick(const std::string& reason) { world.entity().set(RequestKick{.peer = sender.peer, .reason = reason}); }
    void open_view(const std::string& id, const LuaRef& tree);
    void close_view(const std::string& id);
};

struct Command {
    static auto context(flecs::world world, const CommandSender& sender) -> ScriptContext;
    static auto dispatch(flecs::world world, const CommandSender& sender, const std::string& text) -> bool;
    static void view_event(flecs::world world, const CommandSender& sender, uint32_t handler, const std::vector<std::pair<std::string, std::string>>& values);
    static auto command_list(flecs::world world) -> std::vector<CommandInfo>;
    static void open_view(flecs::world world, flecs::entity peer, const std::string& id, const LuaRef& tree);
    static void close_view(flecs::world world, flecs::entity peer, const std::string& id);
};
