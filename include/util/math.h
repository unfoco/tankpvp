#pragma once

#include <cmath>

#include <glm/glm.hpp>

namespace math {

inline auto point_in_box(glm::vec2 p, glm::vec2 center, float angle, float hw, float hh) -> bool {
    glm::vec2 d = p - center;
    float c = std::cos(-angle);
    float s = std::sin(-angle);
    glm::vec2 local((d.x * c) - (d.y * s), (d.x * s) + (d.y * c));
    return std::abs(local.x) <= hw && std::abs(local.y) <= hh;
}

}
