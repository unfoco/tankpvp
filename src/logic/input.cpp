#include <cmath>

#include "component/network.h"
#include "component/object.h"
#include "logic.h"
#include "util/math.h"
#include "util/movement.h"

static auto is_client(flecs::world world) -> bool {
    const auto* cfg = world.try_get<NetworkConfig>();
    return cfg != nullptr && cfg->role == NetworkRole::Client;
}

void Logic::input(flecs::iter& it, size_t i, const InputFlags& flags, const Position& pos, const Rotation& rot, VelocityLinear& vel, VelocityAngular& ang) {
    if (is_client(it.world())) {
        return;
    }
    flecs::entity tank = it.entity(i);
    const auto* ms = tank.try_get<MovementStats>();
    movement::velocity(flags.value, rot.angle, ms ? *ms : MovementStats{}, vel.value, ang.value);

    auto* ammo = tank.try_get_mut<Ammo>();
    bool out_of_ammo = ammo != nullptr && (ammo->reloading > 0.0F || ammo->mag == 0);

    if (flags.has(InputFlags::Shoot) && !out_of_ammo) {
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
        flecs::entity bullet = it.world()
            .entity()
            .set(Position{.value = muzzle})
            .set(Rotation{.angle = aim})
            .set(VelocityLinear{.value = fwd * weapon.speed})
            .set(Decay{.seconds = weapon.life})
            .set(Bullet{.speed = weapon.speed})
            .set(ViewLag{.ticks = view})
            .set(Owner{.peer = peer, .prediction = prediction})
            .add<Replicated>()
            .child_of(tank);
        if (const auto* ps = tank.try_get<ProjectileSprite>(); ps != nullptr && ps->texture != 0) {
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

void Logic::reload(flecs::iter& it, size_t, Ammo& a) {
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

void Logic::camera_decay(flecs::iter& it, size_t i, Camera& cam) {
    if (is_client(it.world())) {
        return;
    }
    if (cam.shake > 0.0F) {
        cam.shake = std::max(0.0F, cam.shake - (it.delta_time() * 1.6F));
        it.entity(i).modified<Camera>();
    }
}

void Logic::flash_decay(flecs::iter& it, size_t i, PostStack& post) {
    if (is_client(it.world())) {
        return;
    }
    if (post.flash > 0.0F) {
        post.flash = std::max(0.0F, post.flash - ((post.flash_fade > 0.0F ? post.flash_fade : 1.0F) * it.delta_time()));
        it.entity(i).modified<PostStack>();
    }
}
