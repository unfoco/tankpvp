#pragma once

#include <SDL3/SDL.h>
#include <flecs.h>

#include "component/event.h"
#include "component/input.h"
#include "component/interface.h"

struct Input {
    Input(flecs::world& world);

   private:
    static void touch(flecs::iter& it);
    static void gather(flecs::iter& it, size_t i, InputState& in);
    static void screen(flecs::iter& it, size_t i, const InterfacePrevious& prev, InterfacePage& page, const WindowEvents& events);
};
