#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <cmath>

constexpr float BULLET_SPEED  = 300.0f;
constexpr float MUZZLE_OFFSET = 30.0f;
constexpr float BULLET_LIFE   = 5.0f;

constexpr uint64_t FIRE_COOLDOWN = 3;

void tank_velocity(uint32_t flags, float angle, glm::vec2& linear, float& angular);

void tank_step(uint32_t flags, glm::vec2& position, float& angle, float dt);

inline bool point_in_obb(glm::vec2 p, glm::vec2 center, float angle, float hw, float hh) {
    glm::vec2 d = p - center;
    float c = std::cos(-angle), s = std::sin(-angle);
    glm::vec2 local(d.x * c - d.y * s, d.x * s + d.y * c);
    return std::abs(local.x) <= hw && std::abs(local.y) <= hh;
}
