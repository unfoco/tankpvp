#include "logic.h"

void Logic::hit(flecs::iter& it, size_t) {
    auto* ev = it.world().try_get<PhysicsEvents>();

    if (!ev) return;
    for (auto& c : ev->sensorBegin) {
        if (!c.entityA.is_alive() || !c.entityB.is_alive()) continue;

        flecs::entity bullet, target;
        if (c.entityA.has<Bullet>() && c.entityB.has<Tank>()) {
            bullet = c.entityA; target = c.entityB;
        } else if (c.entityB.has<Bullet>() && c.entityA.has<Tank>()) {
            bullet = c.entityB; target = c.entityA;
        } else continue;

        target.destruct();
        bullet.destruct();
    }
}
