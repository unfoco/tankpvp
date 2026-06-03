#include "logic/movement.h"

#include <cmath>

#include "component/input.h"

static constexpr float TANK_SPEED = 100.0F;
static constexpr float TANK_TURN = 3.0F;

void tank_velocity(uint32_t flags, float angle, glm::vec2& linear, float& angular) {
    glm::vec2 forward(std::cos(angle), std::sin(angle));

    linear = {0, 0};
    if ((flags & InputFlags::Forward) != 0U) {
        linear = forward * TANK_SPEED;
    } else if ((flags & InputFlags::Backward) != 0U) {
        linear = -forward * TANK_SPEED;
    }

    angular = 0;
    if ((flags & InputFlags::Left) != 0U) {
        angular -= TANK_TURN;
    }
    if ((flags & InputFlags::Right) != 0U) {
        angular += TANK_TURN;
    }
}

void tank_step(uint32_t flags, glm::vec2& position, float& angle, float dt) {
    glm::vec2 linear;
    float angular;
    tank_velocity(flags, angle, linear, angular);
    position += linear * dt;
    angle += angular * dt;
}
