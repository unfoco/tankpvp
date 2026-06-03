#include <cmath>
#include <numbers>

#include "component/network.h"
#include "logic.h"
#include "logic/movement.h"

void Logic::input(flecs::iter& it, size_t i, const InputFlags& flags, const Position& pos, const Rotation& rot, VelocityLinear& vel, VelocityAngular& ang) {
    tank_velocity(flags.value, rot.angle, vel.value, ang.value);

    if (flags.has(InputFlags::Shoot)) {
        flecs::entity tank = it.entity(i);

        uint32_t peer = 0;
        uint32_t prediction = 0;
        uint32_t view = 0;
        if (const auto* o = tank.try_get<Owner>()) {
            peer = o->peer;
        }

        glm::vec2 muzzle = pos.value + MUZZLE_OFFSET * glm::vec2(glm::cos(rot.angle), glm::sin(rot.angle));
        float aim = rot.angle;
        if (const auto* f = tank.try_get<Firing>()) {
            prediction = f->prediction;
            view = f->view;
            if (f->aimed) {
                if (glm::distance(f->muzzle, muzzle) <= 60.0F) {
                    muzzle = f->muzzle;
                }
                if (std::abs(std::remainder(f->aim - rot.angle, 2 * std::numbers::pi)) <= 0.40F) {
                    aim = f->aim;
                }
            }
        }
        glm::vec2 fwd(glm::cos(aim), glm::sin(aim));
        it.world()
            .entity()
            .set(Position{.value = muzzle})
            .set(Rotation{.angle = aim})
            .set(VelocityLinear{.value = fwd * BULLET_SPEED})
            .set(VelocityAngular{})
            .set(CollisionRing{.radius = 3})
            .add<CollisionContinuous>()
            .set(Decay{.seconds = BULLET_LIFE})
            .add<Dynamic>()
            .add<Sensor>()
            .add<Bullet>()
            .set(ViewLag{.ticks = view})
            .set(Owner{.peer = peer, .prediction = prediction})
            .add<Replicated>()
            .child_of(tank);
    }
}
