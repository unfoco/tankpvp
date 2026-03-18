#pragma once

#include <glm/glm.hpp>

struct Color {
    glm::vec3 value;
};

struct Position {
    glm::vec2 value;
};

struct Rotation {
    float angle;
};

struct Bullet {};

struct Tank {};

struct Local {};
