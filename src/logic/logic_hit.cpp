#include "logic.h"

void Logic::hit(flecs::iter& it, size_t, const PhysicsEvents& events) {
    for (auto [bullet, tank] : events.sensor<Bullet, Tank>()) {
        bullet.destruct();
        tank.destruct();
    }
}

