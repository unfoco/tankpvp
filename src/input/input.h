#pragma once

#include <SDL3/SDL.h>
#include <flecs.h>

#include "component/input.h"

struct Input {
    Input(flecs::world&);

private:
    static void update(flecs::iter&, size_t, InputFlags&);
};
