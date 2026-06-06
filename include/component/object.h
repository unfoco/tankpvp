#pragma once

#include <cstdint>

#include <glm/glm.hpp>

struct Position {
    glm::vec2 value;
};

struct Rotation {
    float angle;
};

struct Color {
    glm::vec3 value;
};

struct Decay {
    float seconds;
};

struct Tank {};

struct Bullet {
    float speed = 300.0F;
};

struct MovementStats {
    float speed = 100.0F;
    float turn = 3.0F;
};

struct WeaponStats {
    uint32_t cooldown = 3;
    float speed = 300.0F;
    float muzzle = 30.0F;
    float life = 5.0F;
};
