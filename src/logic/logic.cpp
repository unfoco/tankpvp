#include "logic.h"

#include "component/network.h"
#include "component/object.h"

Logic::Logic(flecs::world& world) {
    world.system<InputFlags, Position, Rotation, VelocityLinear, VelocityAngular>("logic::input").kind(flecs::PreUpdate).with<Tank>().without<Dying>().each(Logic::input);
    world.system<Ammo>("logic::reload").kind(flecs::OnUpdate).each(Logic::reload);
    world.system<Decay>("logic::decay").interval(0.1).kind(flecs::OnUpdate).each(Logic::decay);
}
