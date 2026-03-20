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
    world.system("logic::hit")
        .kind(flecs::PostUpdate)
        .each(Logic::hit);
}
