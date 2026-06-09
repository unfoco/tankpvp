#pragma once

#include <cmath>

#include <glm/glm.hpp>

#include "component/world.h"

namespace ballistics {

enum class Step {
    Fly,
    Bounce,
    Absorb,
};

inline auto tile_at(const WorldGrid& grid, const Tileset& tileset, float wx, float wy) -> const TileType* {
    int tx = static_cast<int>(std::floor(wx / TILE_SIZE));
    int ty = static_cast<int>(std::floor(wy / TILE_SIZE));
    auto [cx, cy] = WorldGrid::chunk_coord(tx, ty);
    auto it = grid.data.find(WorldGrid::key(cx, cy));
    if (it == grid.data.end()) {
        return nullptr;
    }
    uint16_t id = it->second.tiles[WorldGrid::local_index(tx, ty)];
    return id == TILE_EMPTY ? nullptr : &tileset.type(id);
}

inline auto solid(const TileType* t) -> bool {
    return t != nullptr && t->solid;
}

inline auto step(const WorldGrid& grid, const Tileset& tileset, glm::vec2& pos, glm::vec2& vel, float dt, const TileType*& hit, glm::vec2& hit_at) -> Step {
    hit = nullptr;

    if (solid(tile_at(grid, tileset, pos.x, pos.y))) {
        glm::vec2 dir = (glm::length(vel) > 1e-4F) ? glm::normalize(vel) : glm::vec2{1.0F, 0.0F};
        for (int k = 1; k <= 8; ++k) {
            glm::vec2 test = pos - (dir * (TILE_SIZE * 0.2F * static_cast<float>(k)));
            if (!solid(tile_at(grid, tileset, test.x, test.y))) {
                pos = test;
                vel = -vel;
                return Step::Bounce;
            }
        }
        return Step::Absorb;
    }

    glm::vec2 next = pos + (vel * dt);
    const TileType* tx = tile_at(grid, tileset, next.x, pos.y);
    const TileType* ty = tile_at(grid, tileset, pos.x, next.y);
    const TileType* txy = tile_at(grid, tileset, next.x, next.y);
    bool sx = solid(tx);
    bool sy = solid(ty);
    bool sxy = solid(txy);

    if (!sx && !sy && !sxy) {
        pos = next;
        return Step::Fly;
    }

    hit = sx ? tx : (sy ? ty : txy);
    hit_at = sx ? glm::vec2{next.x, pos.y} : (sy ? glm::vec2{pos.x, next.y} : next);
    if (hit->restitution <= 0.0F) {
        return Step::Absorb;
    }

    if (sx) {
        vel.x = -vel.x;
    } else {
        pos.x = next.x;
    }
    if (sy) {
        vel.y = -vel.y;
    } else {
        pos.y = next.y;
    }
    if (!sx && !sy && sxy) {
        vel.x = -vel.x;
        vel.y = -vel.y;
    }
    return Step::Bounce;
}

}
