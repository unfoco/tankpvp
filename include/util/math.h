#pragma once

#include <cmath>
#include <numbers>

#include <glm/glm.hpp>

namespace math {

inline auto heading(float angle) -> glm::vec2 {
    return {std::cos(angle), std::sin(angle)};
}

inline auto length_squared(glm::vec2 v) -> float {
    return (v.x * v.x) + (v.y * v.y);
}

inline auto angle_difference(float a, float b) -> float {
    return std::remainder(a - b, 2.0F * std::numbers::pi_v<float>);
}

inline auto point_in_box(glm::vec2 p, glm::vec2 center, float angle, float hw, float hh) -> bool {
    glm::vec2 d = p - center;
    float c = std::cos(-angle);
    float s = std::sin(-angle);
    glm::vec2 local((d.x * c) - (d.y * s), (d.x * s) + (d.y * c));
    return std::abs(local.x) <= hw && std::abs(local.y) <= hh;
}

}
