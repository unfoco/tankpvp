#include "component/physics.h"
#include "logic.h"

void Logic::input(flecs::iter& it, size_t i, const InputFlags& flags, const Position& pos, const Rotation& rot, VelocityLinear& vel, VelocityAngular& ang) {
    ang.value = 0;
    if ((flags & InputFlags::Left) != InputFlags::None) ang.value -= 3.0f;
    if ((flags & InputFlags::Right) != InputFlags::None) ang.value += 3.0f;

    glm::vec2 fwd(glm::cos(rot.angle), glm::sin(rot.angle));

    vel.value = {0, 0};
    if ((flags & InputFlags::Forward) != InputFlags::None)
        vel.value = fwd * 100.0f;
    else if ((flags & InputFlags::Backward) != InputFlags::None)
        vel.value = -fwd * 100.0f;

    if ((flags & InputFlags::Shoot) != InputFlags::None) {
        it.world().entity()
            .set(Position{.value = pos.value + 30.0f * glm::normalize(fwd)})
            .set(Rotation{.angle = rot.angle})
            .set(VelocityLinear{.value = glm::normalize(fwd) * 300.0f})
            .set(VelocityAngular{})
            .set(CollisionRing{.radius = 3})
            .add<CollisionContinuous>()
            .set(Decay{.seconds = 5.0})
            .add<Dynamic>()
            .add<Sensor>()
            .add<Bullet>()
            .child_of(it.entity(i));
    }
}
