#include "sim.h"

#include "component/network.h"
#include "component/object.h"
#include "component/render.h"

Sim::Sim(flecs::world& world) {
    world.system<InputState, Position, Rotation, VelocityLinear, VelocityAngular>("sim::input").kind(flecs::PreUpdate).with<Controller>().without<Dying>().each(Sim::input);
    world.system<Ammo>("sim::reload").kind(flecs::OnUpdate).each(Sim::reload);
    world.system<Lifetime>("sim::decay").interval(0.1).kind(flecs::OnUpdate).each(Sim::decay);
    world.system<Camera>("sim::camera_decay").kind(flecs::OnUpdate).each(Sim::camera_decay);
    world.system<PostStack>("sim::flash_decay").kind(flecs::OnUpdate).each(Sim::flash_decay);
}
