#include "component/physics.h"
#include "logic.h"

void Logic::input(flecs::iter& it, size_t i, const InputFlags& flags, const Position& pos, const Rotation& rot, VelocityLinear& vel, VelocityAngular& ang) {
    ang.value = 0;
    if (flags.has(InputFlags::Left)) ang.value -= 3.0f;
    if (flags.has(InputFlags::Right)) ang.value += 3.0f;

    glm::vec2 fwd(glm::cos(rot.angle), glm::sin(rot.angle));

    vel.value = {0, 0};
    if (flags.has(InputFlags::Forward))
        vel.value = fwd * 100.0f;
    else if (flags.has(InputFlags::Backward))
        vel.value = -fwd * 100.0f;

    if (flags.has(InputFlags::Shoot)) {
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
