#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

enum class AssetKind : uint8_t {
    Texture = 0,
    Sound = 1,
};

constexpr uint32_t ASSET_MAX_BYTES = 8u * 1024u * 1024u;
constexpr uint64_t ASSET_MAX_TOTAL = 128ull * 1024ull * 1024ull;
constexpr uint32_t ASSET_CHUNK_BYTES = 8000;
constexpr uint32_t ASSET_SEND_BUDGET = 48000;

struct AssetDesc {
    std::string name;
    uint64_t hash = 0;
    AssetKind kind = AssetKind::Texture;
    uint32_t size = 0;
};

struct AssetStore {
    struct Incoming {
        std::string name;
        AssetKind kind = AssetKind::Texture;
        uint32_t total = 0;
        uint32_t received = 0;
        std::vector<uint8_t> buffer;
    };
    std::unordered_map<std::string, uint64_t> names;
    std::unordered_map<std::string, AssetKind> kinds;
    std::unordered_map<uint64_t, std::string> ready;
    std::unordered_map<uint64_t, Incoming> pending;
    uint64_t downloaded = 0;
    uint16_t version = 0;

    uint64_t download_target = 0;
    uint64_t download_have = 0;
    uint32_t download_count = 0;
    uint32_t download_done = 0;

    [[nodiscard]] auto downloading() const -> bool { return download_done < download_count; }
    [[nodiscard]] auto progress() const -> float { return download_target > 0 ? static_cast<float>(static_cast<double>(download_have) / static_cast<double>(download_target)) : 1.0F; }

    [[nodiscard]] auto path_for(const std::string& name) const -> std::string {
        auto n = names.find(name);
        if (n == names.end()) {
            return {};
        }
        auto r = ready.find(n->second);
        return r != ready.end() ? r->second : std::string();
    }
    [[nodiscard]] auto kind_for(const std::string& name) const -> AssetKind {
        auto it = kinds.find(name);
        return it != kinds.end() ? it->second : AssetKind::Texture;
    }
};

struct AssetCatalog {
    std::unordered_map<std::string, uint64_t> names;

    [[nodiscard]] auto hash_of(const std::string& name) const -> uint64_t {
        auto it = names.find(name);
        return it != names.end() ? it->second : 0;
    }
};

struct AssetManifest {
    struct Entry {
        std::string name;
        uint64_t hash = 0;
        AssetKind kind = AssetKind::Texture;
        std::vector<uint8_t> bytes;
    };
    std::vector<Entry> entries;
    uint16_t version = 0;

    [[nodiscard]] auto find(uint64_t hash) const -> const Entry* {
        for (const auto& e : entries) {
            if (e.hash == hash) {
                return &e;
            }
        }
        return nullptr;
    }
};

struct RequestAssetScan {};
struct ResponseAssetScan {};

struct RequestAssetAdopt {
    uint16_t version = 0;
    std::vector<AssetDesc> entries;
};

struct ResponseAssetAdopt {
    std::vector<uint64_t> hashes;
};

struct RequestAssetStore {
    uint64_t hash = 0;
    uint32_t offset = 0;
    uint32_t total = 0;
    std::vector<uint8_t> bytes;
};
