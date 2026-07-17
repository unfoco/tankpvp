#pragma once

#include <cstdint>

#include <glm/glm.hpp>

struct Position {
    glm::vec2 value;
};

struct Rotation {
    float angle;
};

struct Spawn {
    uint16_t epoch = 0;
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

struct Lifetime {
    float seconds = 0.0F;
};

enum class ControlScheme : uint8_t {
    Differential = 0,
    TopDown = 1,
    Platformer = 2,
};

struct Controller {
    ControlScheme scheme = ControlScheme::Differential;
};

struct DifferentialStats {
    float speed = 100.0F;
    float turn = 3.0F;
};

struct TopDownStats {
    float speed = 140.0F;
    float accel = 18.0F;
    float face_rate = 12.0F;
};

struct PlatformerStats {
    float speed = 180.0F;
    float accel = 16.0F;
    float air_control = 0.45F;
    float jump = 380.0F;
};

struct Projectile {
    float speed = 300.0F;
    float gravity_scale = 0.0F;
    uint8_t bounces = 0;
    uint8_t pierce = 0;
    uint64_t sound = 0;
};

struct ProjectileWeapon {
    uint32_t cooldown = 3;
    float speed = 300.0F;
    float muzzle = 30.0F;
    float life = 5.0F;
    uint64_t sound = 0;
};

struct HitBox {};
