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
