#include "logic.h"

void Logic::hit(flecs::iter& it, size_t) {
    auto* ev = it.world().try_get<PhysicsEvents>();

    if (!ev) return;
    for (auto& c : ev->sensor_begin) {
        if (!c.entity_a.is_alive() || !c.entity_b.is_alive()) continue;

        flecs::entity bullet, target;
        if (c.entity_a.has<Bullet>() && c.entity_b.has<Tank>()) {
            bullet = c.entity_a; target = c.entity_b;
        } else if (c.entity_b.has<Bullet>() && c.entity_a.has<Tank>()) {
            bullet = c.entity_b; target = c.entity_a;
        } else continue;

        target.destruct();
        bullet.destruct();
    }
}

