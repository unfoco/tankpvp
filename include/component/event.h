#pragma once

#include <SDL3/SDL.h>

#include "util/fixed_buffer.h"

struct WindowEvents : FixedBuffer<SDL_Event, 256> {
    SDL_Window* target;

    static constexpr int kTextSlots = 64;
    static constexpr int kTextMaxBytes = 64;
    char textPool[kTextSlots][kTextMaxBytes];
    int textPoolCount = 0;

    void pushEvent(const SDL_Event& e) {
        if (full()) {
            return;
        }
        if (e.type == SDL_EVENT_TEXT_INPUT && e.text.text && textPoolCount < kTextSlots) {
            SDL_Event copy = e;
            SDL_strlcpy(textPool[textPoolCount], e.text.text, kTextMaxBytes);
            copy.text.text = textPool[textPoolCount++];
            push(copy);
        } else if (e.type != SDL_EVENT_TEXT_INPUT) {
            push(e);
        }
    }

    void clearAll() {
        clear();
        textPoolCount = 0;
    }
};
