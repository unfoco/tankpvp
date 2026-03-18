#include "logic.h"

Logic::Logic(flecs::world& world) {
    world.system<InputFlags, Position, Rotation>("input::process")
        .kind(flecs::PostUpdate)
        .with<Tank>()
        .each(Logic::input);
}
