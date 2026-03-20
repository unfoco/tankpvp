#include "logic.h"

#include "component/object.h"

Logic::Logic(flecs::world& world) {
    world.system<InputFlags, Position, Rotation, VelocityLinear, VelocityAngular>("logic::input")
        .kind(flecs::PreUpdate)
        .with<Tank>()
        .each(Logic::input);
    world.system<Decay>("logic::decay")
        .interval(0.1)
        .kind(flecs::OnUpdate)
        .each(Logic::decay);

    world.system<PhysicsEvents>("logic::bullet_hit")
        .kind(flecs::PostUpdate)
        .each([](flecs::iter& it, size_t, PhysicsEvents& ev) {
            for (auto& c : ev.sensor_begin) {
                flecs::entity bullet, target;

                if (c.entity_a.is_alive() && c.entity_a.has<Bullet>() && c.entity_b.is_alive() && c.entity_b.has<Tank>()) {
                    bullet = c.entity_a; target = c.entity_b;
                } else if (c.entity_b.is_alive() && c.entity_b.has<Bullet>() && c.entity_a.is_alive() && c.entity_a.has<Tank>()) {
                    bullet = c.entity_b; target = c.entity_a;
                } else {
                    continue;
                }

                bullet.destruct();
            }
        });

}
