#include <cmath>

#include "component/network.h"
#include "component/object.h"
#include "sim.h"
#include "util/ballistics.h"
#include "util/controller.h"
#include "util/math.h"

void Sim::input(flecs::iter& it, size_t i, const InputState& in, const Position& pos, const Rotation& rot, VelocityLinear& vel, VelocityAngular& ang) {
    if (is_client(it.world())) {
        return;
    }
    flecs::entity body = it.entity(i);

    const auto* ctrl = body.try_get<Controller>();
    ControlScheme scheme = ctrl != nullptr ? ctrl->scheme : ControlScheme::Differential;
    if (scheme == ControlScheme::TopDown) {
        const auto* ts = body.try_get<TopDownStats>();
        glm::vec2 cur = vel.value;
        controller::top_down(in, rot.angle, ts != nullptr ? *ts : TopDownStats{}, cur, it.delta_time(), vel.value, ang.value);
    } else if (scheme == ControlScheme::Platformer) {
        const auto* ps = body.try_get<PlatformerStats>();
        PlatformerStats st = ps != nullptr ? *ps : PlatformerStats{};
        const auto* cb = body.try_get<CollisionBox>();
        float hw = cb != nullptr ? cb->width * 0.5F : 0.5F;
        float hh = cb != nullptr ? cb->height * 0.5F : 0.5F;
        const auto* grid = it.world().try_get<WorldGrid>();
        const auto* tileset = it.world().try_get<Tileset>();
        glm::vec2 cur = vel.value;
        bool grounded = (grid != nullptr && tileset != nullptr) && ballistics::grounded(*grid, *tileset, pos.value, hw, hh);
        controller::platformer(in, cur, grounded, st, vel.value, ang.value, it.delta_time());
    } else {
        const auto* ms = body.try_get<DifferentialStats>();
        DifferentialStats ds{.speed = ms != nullptr ? ms->speed : DifferentialStats{}.speed, .turn = ms != nullptr ? ms->turn : DifferentialStats{}.turn};
        controller::differential(in, rot.angle, ds, vel.value, ang.value);
    }

    auto* ammo = body.try_get_mut<Ammo>();
    bool out_of_ammo = ammo != nullptr && (ammo->reloading > 0.0F || ammo->mag == 0);

    if (in.held(button::Primary) && !out_of_ammo) {
        uint32_t peer = 0;
        uint32_t prediction = 0;
        uint32_t view = 0;
        if (const auto* o = body.try_get<Owner>()) {
            peer = o->peer;
        }

        const auto* ws = body.try_get<ProjectileWeapon>();
        ProjectileWeapon weapon = ws ? *ws : ProjectileWeapon{};
        float aim = rot.angle;
        glm::vec2 muzzle = pos.value + weapon.muzzle * math::heading(aim);
        if (const auto* f = body.try_get<Firing>()) {
            prediction = f->prediction;
            view = f->view;
        }
        glm::vec2 fwd(glm::cos(aim), glm::sin(aim));
        flecs::entity bullet = it.world()
            .entity()
            .set(Position{.value = muzzle})
            .set(Rotation{.angle = aim})
            .set(VelocityLinear{.value = fwd * weapon.speed})
            .set(Lifetime{.seconds = weapon.life})
            .set(Projectile{.speed = weapon.speed, .sound = weapon.sound})
            .set(ViewLag{.ticks = view})
            .set(Owner{.peer = peer, .prediction = prediction})
            .add<Replicated>()
            .child_of(body);
        if (const auto* ps = body.try_get<ProjectileSprite>(); ps != nullptr && ps->texture != 0) {
            bullet.set(Sprite{.texture = ps->texture});
        }

        if (ammo != nullptr) {
            ammo->mag -= 1;
            if (ammo->mag == 0 && ammo->reserve > 0) {
                ammo->reloading = ammo->reload_time;
            }
        }
    }
}

void Sim::reload(flecs::iter& it, size_t, Ammo& a) {
    if (is_client(it.world()) || a.reloading <= 0.0F) {
        return;
    }
    a.reloading -= it.delta_time();
    if (a.reloading <= 0.0F) {
        a.reloading = 0.0F;
        uint32_t need = (a.mag_size > a.mag) ? (a.mag_size - a.mag) : 0;
        uint32_t take = (need < a.reserve) ? need : a.reserve;
        a.mag += take;
        a.reserve -= take;
    }
}

