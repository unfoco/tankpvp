#include "logic.h"

Logic::Logic(flecs::world& world) {
    world.system<InputFlags, Position, Rotation>("input::process")
        .kind(flecs::PreUpdate)
        .with<Tank>()
        .each(Logic::input);
    world.system<Position, Velocity>("input::bullet")
        .kind(flecs::OnUpdate)
        .with<Bullet>()
        .each(Logic::bullet);
    world.system<Decay>("input::decay")
        .interval(1.0)
        .kind(flecs::OnUpdate)
        .each(Logic::decay);
}
