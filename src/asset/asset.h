#pragma once

#include <flecs.h>

#include "component/asset.h"

struct Asset {
    Asset(flecs::world& world);

   private:
    static void scan(flecs::entity e);
    static void adopt(flecs::entity e, const RequestAssetAdopt& r);
    static void store(flecs::entity e, const RequestAssetStore& r);
};
