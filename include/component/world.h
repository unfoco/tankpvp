#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <flecs.h>

inline constexpr int CHUNK_SIZE = 32;
inline constexpr int CHUNK_AREA = CHUNK_SIZE * CHUNK_SIZE;
inline constexpr float TILE_SIZE = 32.0F;
inline constexpr uint16_t TILE_EMPTY = 0;

struct TileType {
    uint64_t texture = 0;
    bool solid = true;
    float restitution = 0.0F;
    float friction = 0.5F;
    float drag = 0.0F;
    int32_t hp = 0;
};

struct Tileset {
    std::vector<TileType> types{TileType{}};
    std::vector<std::string> texture_names{std::string{}};
    std::unordered_map<std::string, uint16_t> by_name;
    bool needs_resolve = false;
    uint16_t version = 0;

    auto id_of(const std::string& name) const -> uint16_t {
        auto it = by_name.find(name);
        return it != by_name.end() ? it->second : TILE_EMPTY;
    }
    auto type(uint16_t id) const -> const TileType& {
        return id < types.size() ? types[id] : types[0];
    }
};

struct TileChunk {
    int32_t cx = 0;
    int32_t cy = 0;
    uint16_t tiles[CHUNK_AREA] = {};
};

struct ChunkDirty {};

struct WorldGrid {
    std::unordered_map<int64_t, TileChunk> data;
    std::unordered_map<int64_t, flecs::entity> chunks;
    std::unordered_map<int64_t, int32_t> hp;
    std::vector<int64_t> dirty;
    std::vector<int64_t> updates;
    std::unordered_set<int64_t> generated;
    uint32_t version = 0;

    static auto key(int32_t cx, int32_t cy) -> int64_t {
        return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
    }
    static auto tile_key(int32_t tx, int32_t ty) -> int64_t {
        return (static_cast<int64_t>(tx) << 32) | static_cast<uint32_t>(ty);
    }
    static auto chunk_coord(int32_t tx, int32_t ty) -> std::pair<int32_t, int32_t> {
        auto fd = [](int32_t a) -> int32_t { return a >= 0 ? a / CHUNK_SIZE : -((-a + CHUNK_SIZE - 1) / CHUNK_SIZE); };
        return {fd(tx), fd(ty)};
    }
    static auto local_index(int32_t tx, int32_t ty) -> int {
        auto fm = [](int32_t a) -> int32_t { int32_t m = a % CHUNK_SIZE; return m < 0 ? m + CHUNK_SIZE : m; };
        return (fm(ty) * CHUNK_SIZE) + fm(tx);
    }
};

struct RequestDefineTile {
    uint16_t id = 0;
    TileType type;
    std::string texture;
    std::string name;
};

struct RequestSetTile {
    int32_t tx = 0;
    int32_t ty = 0;
    uint16_t id = 0;
};

struct RequestDamageTile {
    int32_t tx = 0;
    int32_t ty = 0;
    int32_t amount = 0;
};

struct TileUpdateEntry {
    int32_t tx = 0;
    int32_t ty = 0;
    uint16_t id = 0;
};

struct RequestTileUpdate {
    std::vector<TileUpdateEntry> entries;
};

struct RequestLoadTileset {
    std::vector<TileType> types;
};

struct RequestLoadChunk {
    int32_t cx = 0;
    int32_t cy = 0;
    std::vector<uint16_t> tiles;
};

struct RequestUnloadChunk {
    int32_t cx = 0;
    int32_t cy = 0;
};

struct RequestGenerateChunk {
    int32_t cx = 0;
    int32_t cy = 0;
};

struct RequestTileBroadcast {
    int32_t tx = 0;
    int32_t ty = 0;
    uint16_t id = 0;
};
