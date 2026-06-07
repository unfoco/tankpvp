#include "asset.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

static auto content_hash(const uint8_t* data, size_t len) -> uint64_t {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint64_t>(data[i]);
        h *= 1099511628211ull;
    }
    return h;
}

static auto cache_dir() -> std::string {
    return "cache/assets";
}

static auto cache_path(uint64_t hash) -> std::string {
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%016llx", static_cast<unsigned long long>(hash));
    return cache_dir() + "/" + buf.data();
}

static auto kind_for_extension(const std::string& ext) -> AssetKind {
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") {
        return AssetKind::Sound;
    }
    return AssetKind::Texture;
}

static auto is_asset_extension(const std::string& ext) -> bool {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac";
}

void Asset::scan(flecs::entity e) {
    flecs::world world = e.world();
    AssetManifest manifest;
    const auto* previous = world.try_get<AssetManifest>();
    manifest.version = static_cast<uint16_t>((previous != nullptr ? previous->version : 0) + 1);

    if (fs::exists("mods")) {
        std::vector<fs::path> dirs;
        for (const auto& entry : fs::directory_iterator("mods")) {
            if (entry.is_directory() && fs::exists(entry.path() / "assets")) {
                dirs.push_back(entry.path());
            }
        }
        std::sort(dirs.begin(), dirs.end());
        for (const auto& dir : dirs) {
            std::string mod = dir.filename().string();
            fs::path root = dir / "assets";
            for (const auto& file : fs::recursive_directory_iterator(root)) {
                if (!file.is_regular_file() || !is_asset_extension(file.path().extension().string())) {
                    continue;
                }
                std::ifstream stream(file.path(), std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                if (bytes.empty() || bytes.size() > ASSET_MAX_BYTES) {
                    continue;
                }
                std::string name = mod + "/" + fs::relative(file.path(), root).generic_string();
                manifest.entries.push_back({
                    .name = name,
                    .hash = content_hash(bytes.data(), bytes.size()),
                    .kind = kind_for_extension(file.path().extension().string()),
                    .bytes = std::move(bytes),
                });
            }
        }
    }

    std::unordered_map<uint64_t, uint64_t> rehash;
    if (const auto* prev = world.try_get<AssetCatalog>()) {
        for (const auto& entry : manifest.entries) {
            auto it = prev->names.find(entry.name);
            if (it != prev->names.end() && it->second != entry.hash) {
                rehash[it->second] = entry.hash;
            }
        }
    }

    AssetCatalog catalog;
    for (const auto& entry : manifest.entries) {
        catalog.names[entry.name] = entry.hash;
    }
    world.set<AssetCatalog>(std::move(catalog));

    auto& store = world.get_mut<AssetStore>();
    std::error_code wec;
    fs::create_directories(cache_dir(), wec);
    for (const auto& entry : manifest.entries) {
        store.names[entry.name] = entry.hash;
        store.kinds[entry.name] = entry.kind;
        if (store.ready.contains(entry.hash)) {
            continue;
        }
        std::string path = cache_path(entry.hash);
        if (!fs::exists(path, wec)) {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(entry.bytes.data()), static_cast<std::streamsize>(entry.bytes.size()));
        }
        store.ready[entry.hash] = path;
    }

    SDL_Log("asset: manifest built (v%u) with %zu asset(s)", manifest.version, manifest.entries.size());
    world.set<AssetManifest>(std::move(manifest));

    if (!rehash.empty()) {
        world.query_builder<Sprite>().build().each([&](Sprite& s) -> void {
            auto it = rehash.find(s.hash);
            if (it != rehash.end()) {
                s.hash = it->second;
            }
        });
    }

    e.add<ResponseAssetScan>();
}

void Asset::adopt(flecs::entity e, const RequestAssetAdopt& r) {
    auto& store = e.world().get_mut<AssetStore>();
    store.version = r.version;
    std::error_code ec;
    fs::create_directories(cache_dir(), ec);

    std::vector<uint64_t> missing;
    for (const auto& desc : r.entries) {
        store.names[desc.name] = desc.hash;
        store.kinds[desc.name] = desc.kind;
        if (store.ready.contains(desc.hash) || desc.size > ASSET_MAX_BYTES) {
            continue;
        }
        std::string path = cache_path(desc.hash);
        if (fs::exists(path, ec)) {
            store.ready[desc.hash] = path;
            continue;
        }
        if (store.pending.contains(desc.hash) || store.downloaded + desc.size > ASSET_MAX_TOTAL) {
            continue;
        }
        AssetStore::Incoming in;
        in.name = desc.name;
        in.kind = desc.kind;
        in.total = desc.size;
        in.buffer.resize(desc.size);
        store.pending.emplace(desc.hash, std::move(in));
        store.downloaded += desc.size;
        missing.push_back(desc.hash);
    }
    e.set<ResponseAssetAdopt>({.hashes = std::move(missing)});
}

void Asset::store(flecs::entity e, const RequestAssetStore& r) {
    e.destruct();

    auto& store = e.world().get_mut<AssetStore>();
    auto it = store.pending.find(r.hash);
    if (it == store.pending.end()) {
        return;
    }
    AssetStore::Incoming& in = it->second;
    if (r.total != in.total || static_cast<uint64_t>(r.offset) + r.bytes.size() > in.total) {
        return;
    }
    std::copy(r.bytes.begin(), r.bytes.end(), in.buffer.begin() + r.offset);
    in.received += static_cast<uint32_t>(r.bytes.size());
    if (in.received < in.total) {
        return;
    }
    if (content_hash(in.buffer.data(), in.buffer.size()) != r.hash) {
        SDL_Log("asset: hash mismatch for incoming '%s' — dropped", in.name.c_str());
        store.pending.erase(it);
        return;
    }
    std::string path = cache_path(r.hash);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(in.buffer.data()), static_cast<std::streamsize>(in.buffer.size()));
    out.close();
    SDL_Log("asset: installed '%s' (%u bytes)", in.name.c_str(), in.total);
    store.ready[r.hash] = path;
    store.pending.erase(it);
}

Asset::Asset(flecs::world& world) {
    world.set<AssetStore>({});

    world.system("asset::scan").with<RequestAssetScan>().without<ResponseAssetScan>().kind(flecs::OnUpdate).each(Asset::scan);
    world.system<const RequestAssetAdopt>("asset::adopt").without<ResponseAssetAdopt>().kind(flecs::OnUpdate).each(Asset::adopt);
    world.system<const RequestAssetStore>("asset::store").kind(flecs::OnUpdate).each(Asset::store);
}
