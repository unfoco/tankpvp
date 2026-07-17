#include "render.h"
#include "internal.h"
#include <stb/stb_image.h>
#include "component/asset.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

void upload_rgba(RenderState& r, WGPUTexture tex, const uint8_t* pixels, uint32_t w, uint32_t h, uint32_t layer = 0) {
    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = tex;
    dst.origin = {0, 0, layer};
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout lay = {};
    lay.bytesPerRow = w * 4;
    lay.rowsPerImage = h;
    r.queue->writeTexture(dst, pixels, static_cast<size_t>(w) * h * 4, lay, {w, h, 1});
}

auto make_texture_rgba(RenderState& r, const uint8_t* pixels, uint32_t w, uint32_t h) -> GpuTexture {
    GpuTexture out;
    WGPUTextureDescriptor d = {};
    d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    d.dimension = WGPUTextureDimension_2D;
    d.size = {w, h, 1};
    d.format = WGPUTextureFormat_RGBA8Unorm;
    d.mipLevelCount = 1;
    d.sampleCount = 1;
    out.texture = r.device->createTexture(d);
    out.view = wgpu::raii::TextureView(wgpuTextureCreateView(*out.texture, nullptr));
    out.size = {static_cast<float>(w), static_cast<float>(h)};
    upload_rgba(r, *out.texture, pixels, w, h);
    return out;
}

auto hash01(uint32_t x, uint32_t y, uint32_t seed) -> float {
    uint32_t v = (x * 1664525U) ^ (y * 22695477U) ^ (seed * 2654435761U);
    v ^= v >> 16;
    v *= 2246822519U;
    v ^= v >> 13;
    return static_cast<float>(v & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

auto gen_builtin_pixels(uint64_t hash, int& w, int& h) -> std::vector<uint8_t> {
    if (hash == builtin::WHITE) {
        w = h = 1;
        return std::vector<uint8_t>(4, 255);
    }
    if (hash == builtin::DISC) {
        constexpr int N = 64;
        std::vector<uint8_t> px(static_cast<size_t>(N) * N * 4, 255);
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                float dx = (static_cast<float>(x) + 0.5F - (N / 2.0F)) / (N / 2.0F);
                float dy = (static_cast<float>(y) + 0.5F - (N / 2.0F)) / (N / 2.0F);
                float fall = std::clamp(1.0F - std::sqrt((dx * dx) + (dy * dy)), 0.0F, 1.0F);
                px[((static_cast<size_t>(y) * N + x) * 4) + 3] = static_cast<uint8_t>(fall * fall * 255.0F);
            }
        }
        w = h = N;
        return px;
    }
    constexpr int N = 256;
    std::vector<uint8_t> px(static_cast<size_t>(N) * N * 4, 255);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float v = 0.0F;
            float amp = 0.55F;
            for (int o = 0; o < 3; ++o) {
                int cell = 8 << o;
                int step = N / cell;
                int cx = x / step;
                int cy = y / step;
                float fx = static_cast<float>(x % step) / static_cast<float>(step);
                float fy = static_cast<float>(y % step) / static_cast<float>(step);
                fx = fx * fx * (3.0F - 2.0F * fx);
                fy = fy * fy * (3.0F - 2.0F * fy);
                float a = hash01(cx % cell, cy % cell, o);
                float b = hash01((cx + 1) % cell, cy % cell, o);
                float c = hash01(cx % cell, (cy + 1) % cell, o);
                float d = hash01((cx + 1) % cell, (cy + 1) % cell, o);
                v += amp * std::lerp(std::lerp(a, b, fx), std::lerp(c, d, fx), fy);
                amp *= 0.5F;
            }
            auto g = static_cast<uint8_t>(std::clamp(v, 0.0F, 1.0F) * 255.0F);
            uint8_t* p = &px[(static_cast<size_t>(y) * N + x) * 4];
            p[0] = p[1] = p[2] = g;
        }
    }
    w = h = N;
    return px;
}

auto decode_asset(flecs::world world, RenderState& r, uint64_t hash, int* w, int* h) -> uint8_t* {
    const auto* store = world.try_get<AssetStore>();
    if (store == nullptr) {
        return nullptr;
    }
    auto it = store->ready.find(hash);
    if (it == store->ready.end()) {
        return nullptr;
    }
    int comp = 0;
    uint8_t* pixels = stbi_load(it->second.c_str(), w, h, &comp, 4);
    if (pixels == nullptr && r.textures.decode_failed.insert(hash).second) {
        SDL_Log("render: FAILED to decode asset %llu at '%s' (%s)", static_cast<unsigned long long>(hash), it->second.c_str(), stbi_failure_reason());
    }
    return pixels;
}

void resample(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            int x0 = x * sw / dw;
            int x1 = std::max((x + 1) * sw / dw, x0 + 1);
            int y0 = y * sh / dh;
            int y1 = std::max((y + 1) * sh / dh, y0 + 1);
            uint32_t acc[4] = {0, 0, 0, 0};
            uint32_t n = 0;
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    const uint8_t* p = &src[((sy * sw) + sx) * 4];
                    for (int k = 0; k < 4; ++k) {
                        acc[k] += p[k];
                    }
                    ++n;
                }
            }
            uint8_t* o = &dst[((y * dw) + x) * 4];
            for (int k = 0; k < 4; ++k) {
                o[k] = static_cast<uint8_t>(acc[k] / std::max(n, 1U));
            }
        }
    }
}

void atlas_upload_layer(RenderState& r, TextureAtlas& atlas, uint32_t layer, const uint8_t* rgba, int w, int h) {
    std::vector<uint8_t> scaled;
    const uint8_t* data = rgba;
    if (static_cast<uint32_t>(w) != atlas.texels || static_cast<uint32_t>(h) != atlas.texels) {
        scaled.resize(static_cast<size_t>(atlas.texels) * atlas.texels * 4);
        resample(rgba, w, h, scaled.data(), static_cast<int>(atlas.texels), static_cast<int>(atlas.texels));
        data = scaled.data();
    }
    upload_rgba(r, *atlas.array, data, atlas.texels, atlas.texels, layer);
}

}

auto make_builtin(RenderState& r, uint64_t hash) -> GpuTexture {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px = gen_builtin_pixels(hash, w, h);
    return make_texture_rgba(r, px.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
}

void atlas_create(RenderState& r, TextureAtlas& atlas, uint32_t texels, uint32_t layers) {
    WGPUTextureDescriptor d = {};
    d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    d.dimension = WGPUTextureDimension_2D;
    d.size = {texels, texels, layers};
    d.format = WGPUTextureFormat_RGBA8Unorm;
    d.mipLevelCount = 1;
    d.sampleCount = 1;
    wgpu::raii::Texture next = r.device->createTexture(d);

    if (atlas.array && atlas.layers > 0) {
        SDL_assert(atlas.texels == texels);
        WGPUTexelCopyTextureInfo src = {};
        src.texture = *atlas.array;
        src.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyTextureInfo dst = src;
        dst.texture = *next;
        wgpu::raii::CommandEncoder enc = r.device->createCommandEncoder();
        enc->copyTextureToTexture(src, dst, {texels, texels, atlas.layers});
        auto cmd = enc->finish();
        r.queue->submit(1, &cmd);
        wgpuCommandBufferRelease(cmd);
    }
    atlas.array = next;
    WGPUTextureViewDescriptor vd = {};
    vd.dimension = WGPUTextureViewDimension_2DArray;
    vd.format = WGPUTextureFormat_RGBA8Unorm;
    vd.baseMipLevel = 0;
    vd.mipLevelCount = 1;
    vd.baseArrayLayer = 0;
    vd.arrayLayerCount = layers;
    atlas.view = wgpu::raii::TextureView(wgpuTextureCreateView(*atlas.array, &vd));
    atlas.capacity = layers;
    atlas.texels = texels;

    WGPUBindGroupEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].textureView = *atlas.view;
    entries[1].binding = 1;
    entries[1].sampler = *r.samplers.nearest;
    WGPUBindGroupDescriptor bd = {};
    bd.layout = *r.layouts.tile;
    bd.entryCount = 2;
    bd.entries = entries;
    atlas.bind = r.device->createBindGroup(bd);
}

auto Render::texture(flecs::world world, RenderState& r, uint64_t hash) -> GpuTexture* {
    if (hash == 0) {
        return nullptr;
    }
    auto it = r.textures.by_hash.find(hash);
    if (it != r.textures.by_hash.end()) {
        return &it->second;
    }
    if (hash <= builtin::LAST) {
        return &(r.textures.by_hash[hash] = make_builtin(r, hash));
    }
    int w = 0;
    int h = 0;
    uint8_t* pixels = decode_asset(world, r, hash, &w, &h);
    if (pixels == nullptr) {
        return nullptr;
    }
    GpuTexture tex = make_texture_rgba(r, pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    stbi_image_free(pixels);
    return &(r.textures.by_hash[hash] = std::move(tex));
}

auto Render::bind_material(flecs::world world, RenderState& r, uint64_t albedo, uint64_t normal, bool nearest) -> WGPUBindGroup {
    uint64_t key = albedo ^ (normal * 0x9E3779B97F4A7C15ULL) ^ (nearest ? 0x1ULL << 63 : 0);
    auto it = r.textures.binds.find(key);
    if (it != r.textures.binds.end()) {
        return *it->second;
    }
    GpuTexture* a = texture(world, r, albedo);
    bool fallback = false;
    if (a == nullptr) {
        a = texture(world, r, builtin::WHITE);
        fallback = true;
    }
    GpuTexture* n = normal != 0 ? texture(world, r, normal) : nullptr;
    if (n == nullptr) {
        n = texture(world, r, builtin::WHITE);
        if (normal != 0) {
            fallback = true;
        }
    }
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].textureView = *a->view;
    entries[1].binding = 1;
    entries[1].sampler = nearest ? *r.samplers.nearest : *r.samplers.linear;
    entries[2].binding = 2;
    entries[2].textureView = *n->view;
    WGPUBindGroupDescriptor d = {};
    d.layout = *r.layouts.material;
    d.entryCount = 3;
    d.entries = entries;
    wgpu::raii::BindGroup bind = r.device->createBindGroup(d);
    WGPUBindGroup raw = *bind;
    if (!fallback) {
        r.textures.binds.emplace(key, std::move(bind));
    } else {
        r.textures.transient.push_back(std::move(bind));
    }
    return raw;
}

auto Render::atlas_layer(flecs::world world, RenderState& r, TextureAtlas& atlas, uint64_t hash, bool* pending) -> uint32_t {
    if (hash == 0) {
        hash = builtin::WHITE;
    }
    auto it = atlas.layer_of.find(hash);
    if (it != atlas.layer_of.end()) {
        return it->second;
    }
    int w = 0;
    int h = 0;
    uint8_t* pixels = nullptr;
    std::vector<uint8_t> generated;
    if (hash <= builtin::LAST) {
        generated = gen_builtin_pixels(hash, w, h);
        pixels = generated.data();
    } else {
        pixels = decode_asset(world, r, hash, &w, &h);
        if (pixels == nullptr) {
            if (pending != nullptr) {
                *pending = true;
            }
            return 0;
        }
    }

    if (atlas.layers >= atlas.capacity) {
        atlas_create(r, atlas, atlas.texels, atlas.capacity * 2);
    }
    uint32_t layer = atlas.layers++;
    atlas_upload_layer(r, atlas, layer, pixels, w, h);
    if (generated.empty()) {
        stbi_image_free(pixels);
    }
    atlas.layer_of[hash] = layer;
    return layer;
}
