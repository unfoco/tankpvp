#include <cmath>

#include "component/network.h"
#include "logic.h"
#include "util/math.h"
#include "util/movement.h"

void Logic::input(flecs::iter& it, size_t i, const InputFlags& flags, const Position& pos, const Rotation& rot, VelocityLinear& vel, VelocityAngular& ang) {
    flecs::entity tank = it.entity(i);
    const auto* ms = tank.try_get<MovementStats>();
    movement::velocity(flags.value, rot.angle, ms ? *ms : MovementStats{}, vel.value, ang.value);

    if (flags.has(InputFlags::Shoot)) {

        uint32_t peer = 0;
        uint32_t prediction = 0;
        uint32_t view = 0;
        if (const auto* o = tank.try_get<Owner>()) {
            peer = o->peer;
        }

        const auto* ws = tank.try_get<WeaponStats>();
        WeaponStats weapon = ws ? *ws : WeaponStats{};
        glm::vec2 muzzle = pos.value + weapon.muzzle * math::heading(rot.angle);
        float aim = rot.angle;
        if (const auto* f = tank.try_get<Firing>()) {
            prediction = f->prediction;
            view = f->view;
            if (f->aimed) {
                if (glm::distance(f->muzzle, muzzle) <= 60.0F) {
                    muzzle = f->muzzle;
                }
                if (std::abs(math::angle_difference(f->aim, rot.angle)) <= 0.40F) {
                    aim = f->aim;
                }
            }
        }
        glm::vec2 fwd(glm::cos(aim), glm::sin(aim));
        it.world()
            .entity()
            .set(Position{.value = muzzle})
            .set(Rotation{.angle = aim})
            .set(VelocityLinear{.value = fwd * weapon.speed})
            .set(VelocityAngular{})
            .set(CollisionRing{.radius = 3})
            .add<CollisionContinuous>()
            .set(Decay{.seconds = weapon.life})
            .add<Dynamic>()
            .add<Sensor>()
            .set(Bullet{.speed = weapon.speed})
            .set(ViewLag{.ticks = view})
            .set(Owner{.peer = peer, .prediction = prediction})
            .add<Replicated>()
            .child_of(tank);
    }
}
