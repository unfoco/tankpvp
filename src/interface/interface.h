#pragma once

#include <clay.h>
#include <flecs.h>

#include "component/event.h"
#include "component/interface.h"
#include "widget/widget.h"

struct Interface {
    Interface(flecs::world& world);

   private:
    static void frame(flecs::iter& it, size_t i, InterfaceState& state);
    static void event(flecs::iter& it, size_t i, InterfaceState& state, const WindowEvents& events);
    static void build(flecs::iter& it, size_t, InterfaceState& state, InterfaceCommands& cmds, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events);

   public:
    static auto main(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto host(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto pause(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto ingame(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto server(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto connect(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto settings(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto chat(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto status(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto content(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
};
