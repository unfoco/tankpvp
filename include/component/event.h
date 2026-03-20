#pragma once

#include <SDL3/SDL.h>

#include <type/fixed_buffer.h>

struct EventQueue : FixedBuffer<SDL_Event, 256> {};
