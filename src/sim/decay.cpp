#include <algorithm>

#include "sim.h"

void Sim::decay(flecs::entity e, Lifetime& decay) {
    decay.seconds -= 0.1;
    if (decay.seconds <= 0) {
        e.destruct();
    }
}

void Sim::camera_decay(flecs::iter& it, size_t i, Camera& cam) {
    if (is_client(it.world())) {
        return;
    }
    if (cam.shake > 0.0F) {
        cam.shake = std::max(0.0F, cam.shake - (it.delta_time() * 1.6F));
        it.entity(i).modified<Camera>();
    }
}

void Sim::flash_decay(flecs::iter& it, size_t i, PostStack& post) {
    if (is_client(it.world())) {
        return;
    }
    if (post.flash > 0.0F) {
        post.flash = std::max(0.0F, post.flash - ((post.flash_fade > 0.0F ? post.flash_fade : 1.0F) * it.delta_time()));
        it.entity(i).modified<PostStack>();
    }
}
