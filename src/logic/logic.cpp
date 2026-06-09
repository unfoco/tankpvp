#include "logic.h"

#include "component/network.h"
#include "component/object.h"

Logic::Logic(flecs::world& world) {
    const auto* cfg = world.try_get<NetworkConfig>();
    if (cfg == nullptr || cfg->role != NetworkRole::Client) {
        world.system<InputFlags, Position, Rotation, VelocityLinear, VelocityAngular>("logic::input").kind(flecs::PreUpdate).with<Tank>().each(Logic::input);
    }
    world.system<Decay>("logic::decay").interval(0.1).kind(flecs::OnUpdate).each(Logic::decay);
}
