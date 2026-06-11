#pragma once

#include <cstdint>

#include <glm/glm.hpp>

struct RequestEffect {
    glm::vec2 position{0};
    float angle = 0;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
};

struct RequestParticles {
    glm::vec2 position{0};
    float dir = 0;
    float spread = 6.2832F;
    uint16_t count = 12;
    uint64_t texture = 0;
    float speed_min = 40;
    float speed_max = 120;
    float size_min = 8;
    float size_max = 18;
    float life_min = 0.5F;
    float life_max = 1.0F;
    float gravity = 0;
    float drag = 1.5F;
    float spin = 9.0F;
    float grow = 0;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t alpha = 255;
    bool additive = false;
};
