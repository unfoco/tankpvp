#pragma once

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

#include "component/input.h"
#include "component/object.h"

namespace movement {

inline void velocity(uint32_t flags, float angle, const MovementStats& stats, glm::vec2& linear, float& angular) {
    glm::vec2 forward(std::cos(angle), std::sin(angle));

    linear = {0, 0};
    if ((flags & InputFlags::Forward) != 0U) {
        linear = forward * stats.speed;
    } else if ((flags & InputFlags::Backward) != 0U) {
        linear = -forward * stats.speed;
    }

    angular = 0;
    if ((flags & InputFlags::Left) != 0U) {
        angular -= stats.turn;
    }
    if ((flags & InputFlags::Right) != 0U) {
        angular += stats.turn;
    }
}

inline void step(uint32_t flags, const MovementStats& stats, glm::vec2& position, float& angle, float dt) {
    glm::vec2 linear;
    float angular;
    velocity(flags, angle, stats, linear, angular);
    position += linear * dt;
    angle += angular * dt;
}

}
