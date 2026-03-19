#include "logic.h"
#include "component/object.h"

Logic::Logic(flecs::world& world) {
    world.system<InputFlags, Position, Velocity, Rotation>("input::process")
        .kind(flecs::PreUpdate)
        .with<Tank>()
        .each(Logic::input);
    world.system<Velocity, Position>("input::velocity")
        .kind(flecs::OnUpdate)
        .each(Logic::velocity);
    world.system<Decay>("input::decay")
        .interval(0.1)
        .kind(flecs::OnUpdate)
        .each(Logic::decay);
}
