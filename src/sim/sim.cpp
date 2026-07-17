#include "sim.h"

#include "component/network.h"
#include "component/object.h"
#include "component/render.h"

Sim::Sim(flecs::world& world) {
    world.system<InputState, Position, Rotation, VelocityLinear, VelocityAngular>("sim::input").kind(flecs::PreUpdate).with<Controller>().without<Dying>().each(Sim::input);
    flecs::entity post_physics = world.lookup("physics::Post");
    flecs::entity fire_phase = world.entity("sim::Fire").add(flecs::Phase).depends_on(post_physics ? post_physics : world.entity(flecs::PostUpdate));
    world.system<const InputState, const Position, const Rotation>("sim::fire").kind(fire_phase).with<Controller>().without<Dying>().each(Sim::fire);
    world.system<Ammo>("sim::reload").kind(flecs::OnUpdate).each(Sim::reload);
    world.system<Lifetime>("sim::decay").interval(0.1).kind(flecs::OnUpdate).each(Sim::decay);
    world.system<Camera>("sim::camera_decay").kind(flecs::OnUpdate).each(Sim::camera_decay);
    world.system<PostStack>("sim::flash_decay").kind(flecs::OnUpdate).each(Sim::flash_decay);
}
