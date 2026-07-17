#pragma once

#include <flecs.h>

#include <cstdint>

#include <glm/glm.hpp>

namespace button {
inline constexpr uint16_t Primary = 1U << 0;
inline constexpr uint16_t Secondary = 1U << 1;
inline constexpr uint16_t Action0 = 1U << 2;
inline constexpr uint16_t Action1 = 1U << 3;
inline constexpr uint16_t Action2 = 1U << 4;
inline constexpr uint16_t Action3 = 1U << 5;
inline constexpr uint16_t ACTION_COUNT = 4;
}

struct InputState {
    glm::vec2 move{0.0F};
    glm::vec2 aim{0.0F};
    uint16_t buttons = 0;
    uint16_t pressed = 0;

    [[nodiscard]] auto held(uint16_t b) const -> bool {
        return (buttons & b) != 0U;
    }
    [[nodiscard]] auto down(uint16_t b) const -> bool {
        return (pressed & b) != 0U;
    }
};

struct Pointer {
    glm::vec2 world{0.0F};
    bool valid = false;
};
