#pragma once

#include <SDL3/SDL.h>
#include <flecs.h>

#include "component/event.h"
#include "component/input.h"
#include "component/interface.h"

struct Input {
    Input(flecs::world&);

private:
    static void tank(flecs::iter&, size_t, InputFlags&);
    static void screen(flecs::iter&, size_t, const InterfacePrevious&, InterfacePage&, const WindowEvents&);
};
