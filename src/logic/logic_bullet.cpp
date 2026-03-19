#include "logic.h"

void Logic::bullet(flecs::iter& it, size_t, Position& pos, const Velocity& vel) {
    pos.value += (3.0f * vel.value);
}
