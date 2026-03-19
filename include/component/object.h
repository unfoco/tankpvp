#pragma once

#include <glm/glm.hpp>

struct Velocity {
    glm::vec2 value;
};

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

struct Bullet {};

struct Tank {};

struct Local {};
