#pragma once

#include <SDL3/SDL.h>

#include "util/fixed_buffer.h"

struct WindowEvents : FixedBuffer<SDL_Event, 256> {
    SDL_Window* target;
};
