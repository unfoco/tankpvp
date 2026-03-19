#include "logic.h"

void Logic::velocity(flecs::iter& it, size_t, const Velocity& vel, Position& pos) {
    pos.value += vel.value * it.delta_time();
}
