#include "logic.h"

// todo: velocity based movement?
void Logic::input(flecs::iter& it, size_t i, const InputFlags& flags, Position& pos, Rotation& rot) {
    float dt = it.delta_time();

    float rot_speed = 3.0f * dt;
    if ((flags & InputFlags::Left) != InputFlags::None)  rot.angle -= rot_speed;
    if ((flags & InputFlags::Right) != InputFlags::None) rot.angle += rot_speed;

    glm::vec2 forward(glm::cos(rot.angle), glm::sin(rot.angle));

    float move_speed = 100.0f * dt;
    if ((flags & InputFlags::Forward) != InputFlags::None) {
        pos.value.x += forward.x * move_speed;
        pos.value.y += forward.y * move_speed;
    }
    if ((flags & InputFlags::Backward) != InputFlags::None) {
        pos.value.x -= forward.x * move_speed;
        pos.value.y -= forward.y * move_speed;
    }

    if ((flags & InputFlags::Shoot) != InputFlags::None) {
        it.world().entity()
            .set(Position{.value = pos.value})
            .add<Bullet>()
            .child_of(it.entities()[i]);
    }
}
