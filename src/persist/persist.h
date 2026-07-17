#pragma once

#include <flecs.h>

#include <string>
#include <vector>

#include "component/interface.h"

struct PersistConfig {
    std::string username;
    float volume = 1.0F;
    float music = 1.0F;
    float render_scale = 1.0F;
    float light_scale = 0.5F;
    bool bloom = true;
    std::vector<ServerEntry> servers;

    auto operator==(const PersistConfig&) const -> bool = default;
};

struct PersistState {
    PersistConfig last;
    bool dirty = false;
    double saveAt = 0.0;
};

struct Persist {
    Persist(flecs::world& world);

   private:
    static void save(flecs::iter& it, size_t i, PersistState& state);
};
