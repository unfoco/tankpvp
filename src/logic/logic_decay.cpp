#include "logic.h"

void Logic::decay(flecs::entity e, Decay& decay) {
    if (--decay.seconds <= 0) e.destruct();
}
