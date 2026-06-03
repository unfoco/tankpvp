#pragma once

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

constexpr float BULLET_SPEED = 300.0F;
constexpr float MUZZLE_OFFSET = 30.0F;
constexpr float BULLET_LIFE = 5.0F;

constexpr uint64_t FIRE_COOLDOWN = 3;

void tank_velocity(uint32_t flags, float angle, glm::vec2& linear, float& angular);

void tank_step(uint32_t flags, glm::vec2& position, float& angle, float dt);

inline auto point_in_obb(glm::vec2 p, glm::vec2 center, float angle, float hw, float hh) -> bool {
    glm::vec2 d = p - center;
    float c = std::cos(-angle);
    float s = std::sin(-angle);
    glm::vec2 local((d.x * c) - (d.y * s), (d.x * s) + (d.y * c));
    return std::abs(local.x) <= hw && std::abs(local.y) <= hh;
}
