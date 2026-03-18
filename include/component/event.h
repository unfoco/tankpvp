#pragma once

#include <SDL3/SDL.h>

struct EventQueue {
    static constexpr int MAX_EVENTS = 256;

    SDL_Event events[MAX_EVENTS];
    int count = 0;

    inline void push(const SDL_Event& e) {
        if (count < MAX_EVENTS) events[count++] = e;
    }

    inline void clear() { count = 0; }

    const SDL_Event* begin() const { return events; }
    const SDL_Event* end() const   { return events + count; }

    SDL_Event* begin() { return events; }
    SDL_Event* end()   { return events + count; }
};
