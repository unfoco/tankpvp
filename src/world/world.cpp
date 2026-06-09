#include "world.h"

#include <array>
#include <cmath>
#include <utility>

#include "component/asset.h"
#include "component/network.h"
#include "component/object.h"

static auto tile_at(flecs::world world, float wx, float wy) -> const TileType* {
    const auto* grid = world.try_get<WorldGrid>();
    const auto* tileset = world.try_get<Tileset>();
    if (grid == nullptr || tileset == nullptr) {
        return nullptr;
    }
    int tx = static_cast<int>(std::floor(wx / TILE_SIZE));
    int ty = static_cast<int>(std::floor(wy / TILE_SIZE));
    auto [cx, cy] = WorldGrid::chunk_coord(tx, ty);
    auto it = grid->data.find(WorldGrid::key(cx, cy));
    if (it == grid->data.end()) {
        return nullptr;
    }
    uint16_t id = it->second.tiles[WorldGrid::local_index(tx, ty)];
    return id == TILE_EMPTY ? nullptr : &tileset->type(id);
}

static void enqueue_neighbors(WorldGrid& grid, int32_t tx, int32_t ty) {
    if (grid.updates.size() > 100000) {
        return;
    }
    grid.updates.push_back(WorldGrid::tile_key(tx, ty));
    grid.updates.push_back(WorldGrid::tile_key(tx + 1, ty));
    grid.updates.push_back(WorldGrid::tile_key(tx - 1, ty));
    grid.updates.push_back(WorldGrid::tile_key(tx, ty + 1));
    grid.updates.push_back(WorldGrid::tile_key(tx, ty - 1));
}

World::World(flecs::world& world) {
    world.component<RequestDefineTile>();
    world.component<RequestSetTile>();
    world.component<Tileset>().add(flecs::Singleton);
    world.component<WorldGrid>().add(flecs::Singleton);
    world.set<Tileset>({});
    world.set<WorldGrid>({});

    world.observer<const RequestDefineTile>("world::define").event(flecs::OnSet).each(World::define);
    world.observer<const RequestSetTile>("world::set").event(flecs::OnSet).each(World::set);
    world.observer<const RequestLoadTileset>("world::tileset").event(flecs::OnSet).each(World::tileset);
    world.observer<const RequestLoadChunk>("world::load").event(flecs::OnSet).each(World::load);
    world.observer<const RequestUnloadChunk>("world::unload").event(flecs::OnSet).each(World::unload);
    world.observer<const RequestDamageTile>("world::damage").event(flecs::OnSet).each(World::damage);

    world.system("world::sync").kind(flecs::OnUpdate).run(World::sync);
    world.system("world::updates").kind(flecs::OnUpdate).run(World::updates);
    world.system("world::resolve").kind(flecs::OnUpdate).run(World::resolve);

    flecs::entity pi = world.lookup("physics::Init");
    flecs::entity po = world.lookup("physics::Post");
    world.system<const TileChunk>("world::mesh").with<ChunkDirty>().kind(pi ? pi : flecs::OnUpdate).each(World::mesh);
    world.system<const Position, VelocityLinear>("world::drag").with<Tank>().kind(pi ? pi : flecs::OnUpdate).each(World::drag);
    world.system("world::collide").kind(po ? po : flecs::PostUpdate).run(World::collide);
}

void World::define(flecs::entity e, const RequestDefineTile& req) {
    auto& tileset = e.world().get_mut<Tileset>();
    if (req.id >= tileset.types.size()) {
        tileset.types.resize(static_cast<size_t>(req.id) + 1);
        tileset.texture_names.resize(static_cast<size_t>(req.id) + 1);
    }
    tileset.types[req.id] = req.type;
    tileset.texture_names[req.id] = req.texture;
    if (!req.name.empty()) {
        tileset.by_name[req.name] = req.id;
    }
    tileset.needs_resolve = tileset.needs_resolve || !req.texture.empty();
    ++tileset.version;
    e.destruct();
}

void World::set(flecs::entity e, const RequestSetTile& req) {
    auto& grid = e.world().get_mut<WorldGrid>();
    auto [cx, cy] = WorldGrid::chunk_coord(req.tx, req.ty);
    int64_t key = WorldGrid::key(cx, cy);
    TileChunk& chunk = grid.data[key];
    chunk.cx = cx;
    chunk.cy = cy;
    int index = WorldGrid::local_index(req.tx, req.ty);
    if (chunk.tiles[index] != req.id) {
        chunk.tiles[index] = req.id;
        grid.dirty.push_back(key);
        ++grid.version;
        enqueue_neighbors(grid, req.tx, req.ty);
        e.world().entity().set(RequestTileBroadcast{.tx = req.tx, .ty = req.ty, .id = req.id});
    }
    e.destruct();
}

void World::unload(flecs::entity e, const RequestUnloadChunk& req) {
    auto& grid = e.world().get_mut<WorldGrid>();
    int64_t key = WorldGrid::key(req.cx, req.cy);
    if (auto cit = grid.chunks.find(key); cit != grid.chunks.end()) {
        if (cit->second.is_alive()) {
            cit->second.destruct();
        }
        grid.chunks.erase(cit);
    }
    grid.data.erase(key);
    ++grid.version;
    e.destruct();
}

void World::tileset(flecs::entity e, const RequestLoadTileset& req) {
    auto& tileset = e.world().get_mut<Tileset>();
    tileset.types = req.types;
    tileset.texture_names.assign(req.types.size(), std::string{});
    e.destruct();
}

void World::load(flecs::entity e, const RequestLoadChunk& req) {
    auto& grid = e.world().get_mut<WorldGrid>();
    int64_t key = WorldGrid::key(req.cx, req.cy);
    TileChunk& chunk = grid.data[key];
    chunk.cx = req.cx;
    chunk.cy = req.cy;
    for (size_t i = 0; i < req.tiles.size() && i < CHUNK_AREA; ++i) {
        chunk.tiles[i] = req.tiles[i];
    }
    grid.dirty.push_back(key);
    ++grid.version;
    e.destruct();
}

void World::damage(flecs::entity e, const RequestDamageTile& req) {
    flecs::world world = e.world();
    auto& grid = world.get_mut<WorldGrid>();
    const auto* tileset = world.try_get<Tileset>();
    auto [cx, cy] = WorldGrid::chunk_coord(req.tx, req.ty);
    auto cit = grid.data.find(WorldGrid::key(cx, cy));
    if (cit == grid.data.end() || tileset == nullptr) {
        e.destruct();
        return;
    }
    int index = WorldGrid::local_index(req.tx, req.ty);
    uint16_t id = cit->second.tiles[index];
    int32_t maxhp = (id == TILE_EMPTY) ? 0 : tileset->type(id).hp;
    if (maxhp > 0) {
        int64_t tk = WorldGrid::tile_key(req.tx, req.ty);
        auto hit = grid.hp.find(tk);
        int32_t cur = (hit != grid.hp.end() ? hit->second : maxhp) - req.amount;
        if (cur <= 0) {
            cit->second.tiles[index] = TILE_EMPTY;
            grid.hp.erase(tk);
            grid.dirty.push_back(WorldGrid::key(cx, cy));
            ++grid.version;
            enqueue_neighbors(grid, req.tx, req.ty);
            world.entity().set(RequestTileBroadcast{.tx = req.tx, .ty = req.ty, .id = TILE_EMPTY});
        } else {
            grid.hp[tk] = cur;
        }
    }
    e.destruct();
}

void World::sync(flecs::iter& it) {
    flecs::world world = it.world();
    auto& grid = world.get_mut<WorldGrid>();
    if (grid.dirty.empty()) {
        return;
    }
    for (int64_t key : grid.dirty) {
        auto dit = grid.data.find(key);
        if (dit == grid.data.end()) {
            continue;
        }
        auto cit = grid.chunks.find(key);
        flecs::entity e = (cit != grid.chunks.end() && cit->second.is_alive()) ? cit->second : world.entity();
        grid.chunks[key] = e;
        e.set<TileChunk>(dit->second);
        e.add<ChunkDirty>();
    }
    grid.dirty.clear();
}

void World::updates(flecs::iter& it) {
    flecs::world world = it.world();
    auto& grid = world.get_mut<WorldGrid>();
    if (grid.updates.empty()) {
        return;
    }
    constexpr size_t CAP = 256;
    size_t n = grid.updates.size() < CAP ? grid.updates.size() : CAP;
    RequestTileUpdate batch;
    batch.entries.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        int64_t key = grid.updates[i];
        auto tx = static_cast<int32_t>(key >> 32);
        auto ty = static_cast<int32_t>(static_cast<uint32_t>(key));
        auto [cx, cy] = WorldGrid::chunk_coord(tx, ty);
        auto cit = grid.data.find(WorldGrid::key(cx, cy));
        uint16_t id = (cit != grid.data.end()) ? cit->second.tiles[WorldGrid::local_index(tx, ty)] : TILE_EMPTY;
        batch.entries.push_back({.tx = tx, .ty = ty, .id = id});
    }
    grid.updates.erase(grid.updates.begin(), grid.updates.begin() + static_cast<std::ptrdiff_t>(n));
    if (!batch.entries.empty()) {
        world.entity().set(std::move(batch));
    }
}

void World::resolve(flecs::iter& it) {
    flecs::world world = it.world();
    auto& tileset = world.get_mut<Tileset>();
    if (!tileset.needs_resolve) {
        return;
    }
    const auto* catalog = world.try_get<AssetCatalog>();
    if (catalog == nullptr) {
        return;
    }
    bool pending = false;
    for (size_t id = 1; id < tileset.types.size(); ++id) {
        const std::string& name = tileset.texture_names[id];
        if (tileset.types[id].texture == 0 && !name.empty()) {
            tileset.types[id].texture = catalog->hash_of(name);
            if (tileset.types[id].texture == 0) {
                pending = true;
            }
        }
    }
    tileset.needs_resolve = pending;
}

void World::mesh(flecs::iter& it, size_t i, const TileChunk& chunk) {
    flecs::entity e = it.entity(i);
    const auto* tileset = it.world().try_get<Tileset>();
    auto is_solid = [&](uint16_t id) -> bool { return id != TILE_EMPTY && tileset != nullptr && tileset->type(id).solid; };

    const float ox = static_cast<float>(chunk.cx) * CHUNK_SIZE * TILE_SIZE;
    const float oy = static_cast<float>(chunk.cy) * CHUNK_SIZE * TILE_SIZE;

    CollisionMesh m;
    std::array<bool, CHUNK_AREA> used{};
    for (int y = 0; y < CHUNK_SIZE; ++y) {
        for (int x = 0; x < CHUNK_SIZE;) {
            const int idx = (y * CHUNK_SIZE) + x;
            const uint16_t id = chunk.tiles[idx];
            if (used[idx] || !is_solid(id)) {
                ++x;
                continue;
            }
            int w = 1;
            while (x + w < CHUNK_SIZE && !used[(y * CHUNK_SIZE) + x + w] && chunk.tiles[(y * CHUNK_SIZE) + x + w] == id) {
                ++w;
            }
            int h = 1;
            for (bool grow = true; grow && y + h < CHUNK_SIZE; ++h) {
                for (int k = 0; k < w; ++k) {
                    int i2 = ((y + h) * CHUNK_SIZE) + x + k;
                    if (used[i2] || chunk.tiles[i2] != id) {
                        grow = false;
                        break;
                    }
                }
                if (!grow) {
                    break;
                }
            }
            for (int dy = 0; dy < h; ++dy) {
                for (int dx = 0; dx < w; ++dx) {
                    used[((y + dy) * CHUNK_SIZE) + x + dx] = true;
                }
            }
            const TileType& type = tileset->type(id);
            m.boxes.push_back({
                .center = {ox + ((static_cast<float>(x) + (static_cast<float>(w) * 0.5F)) * TILE_SIZE), oy + ((static_cast<float>(y) + (static_cast<float>(h) * 0.5F)) * TILE_SIZE)},
                .half = {static_cast<float>(w) * TILE_SIZE * 0.5F, static_cast<float>(h) * TILE_SIZE * 0.5F},
                .restitution = type.restitution,
                .friction = type.friction,
            });
            x += w;
        }
    }

    e.set(CollisionLayers{.memberships = 0x0002});
    e.set(std::move(m));
}

void World::drag(flecs::iter& it, size_t, const Position& pos, VelocityLinear& vel) {
    const TileType* t = tile_at(it.world(), pos.value.x, pos.value.y);
    if (t == nullptr || t->solid || t->drag <= 0.0F) {
        return;
    }
    vel.value *= std::exp(-t->drag * it.delta_time());
}

void World::collide(flecs::iter& it) {
    flecs::world world = it.world();
    const auto& ev = world.get<PhysicsEvents>();
    for (const ContactEvent& c : ev.contactBegin) {
        flecs::entity a = c.entity_a;
        flecs::entity b = c.entity_b;
        flecs::entity bullet = (a.is_alive() && a.has<Bullet>()) ? a : ((b.is_alive() && b.has<Bullet>()) ? b : flecs::entity());
        flecs::entity wall = (a.is_alive() && a.has<TileChunk>()) ? a : ((b.is_alive() && b.has<TileChunk>()) ? b : flecs::entity());
        if (!bullet.is_alive() || !wall.is_alive()) {
            continue;
        }
        float dir = (wall == b) ? 1.0F : -1.0F;
        float sx = c.point.x + (c.normal.x * dir * TILE_SIZE * 0.5F);
        float sy = c.point.y + (c.normal.y * dir * TILE_SIZE * 0.5F);
        const TileType* t = tile_at(world, sx, sy);
        if (t == nullptr || !t->solid) {
            continue;
        }
        if (t->hp > 0 && !bullet.has<Predicted>()) {
            world.entity().set(RequestDamageTile{.tx = static_cast<int32_t>(std::floor(sx / TILE_SIZE)), .ty = static_cast<int32_t>(std::floor(sy / TILE_SIZE)), .amount = 25});
        }
        if (t->restitution <= 0.0F) {
            bullet.destruct();
        }
    }
}
