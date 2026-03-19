#include "logic.h"

void Logic::decay(flecs::entity e, Decay& decay) {
    decay.seconds -= 0.1;
    if (decay.seconds <= 0) e.destruct();
}
