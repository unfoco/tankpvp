#pragma once

#include <SDL3/SDL.h>

#include "util/fixed_buffer.h"

struct WindowEvents : FixedBuffer<SDL_Event, 256> {
    SDL_Window* target = nullptr;
    char text_arena[4096];
    int text_used = 0;

    void push(const SDL_Event& src) {
        if (full()) {
            return;
        }
        SDL_Event copy = src;
        if (src.type == SDL_EVENT_TEXT_INPUT && src.text.text != nullptr) {
            size_t len = SDL_strlen(src.text.text) + 1;
            if (text_used + static_cast<int>(len) > static_cast<int>(sizeof(text_arena))) {
                return;
            }
            char* dst = text_arena + text_used;
            SDL_memcpy(dst, src.text.text, len);
            text_used += static_cast<int>(len);
            copy.text.text = dst;
        }
        FixedBuffer::push(copy);
    }

    void clear() {
        text_used = 0;
        FixedBuffer::clear();
    }
};
