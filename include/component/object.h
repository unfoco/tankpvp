#pragma once

#include <cstdint>

#include <glm/glm.hpp>

struct Position {
    glm::vec2 value;
};

struct Rotation {
    float angle;
};

struct Decay {
    float seconds;
};

struct Tank {};

struct Spawn {
    uint16_t epoch = 0;
};

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

struct ProjectileSprite {
    uint64_t texture = 0;
};

struct Ammo {
    uint32_t mag = 30;
    uint32_t reserve = 90;
    uint32_t mag_size = 30;
    float reload_time = 1.8F;
    float reloading = 0.0F;
};
