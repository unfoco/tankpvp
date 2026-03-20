#include "logic.h"

void Logic::hit(flecs::iter& it, size_t, const PhysicsEvents& events) {
    events.eachSensor<Bullet, Tank>([](flecs::entity bullet, flecs::entity tank) {
        tank.destruct();
        bullet.destruct();
    });
}
