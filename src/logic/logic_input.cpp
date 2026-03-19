#include "logic.h"

void Logic::input(flecs::iter& it, size_t i, const InputFlags& flags, const Position& pos, Velocity& vel, Rotation& rot) {
    float rotSpeed = 3.0f * it.delta_time();
    if ((flags & InputFlags::Left) != InputFlags::None)  rot.angle -= rotSpeed;
    if ((flags & InputFlags::Right) != InputFlags::None) rot.angle += rotSpeed;

    glm::vec2 forward(glm::cos(rot.angle), glm::sin(rot.angle));
    float move_speed = 100.0f;

    vel.value = glm::vec2(0.0f, 0.0f);
    if ((flags & InputFlags::Forward) != InputFlags::None) {
        vel.value = forward * move_speed;
    } else if ((flags & InputFlags::Backward) != InputFlags::None) {
        vel.value = -forward * move_speed;
    }

    if ((flags & InputFlags::Shoot) != InputFlags::None) {
        it.world().entity()
            .set(Position{.value = pos.value + 30.0f * glm::normalize(forward)})
            .set(Velocity{.value = glm::normalize(forward) * 300.0f})
            .set(Decay{.seconds = 5.0})
            .add<Bullet>()
            .child_of(it.entities()[i]);
    }
}
