#include "render.h"

#include <SDL3_image/SDL_image.h>

#include "component/world.h"

namespace {

struct TileView {
    int windowW = 0, windowH = 0;
    float size = 0;
};

auto view_of(const RenderState& render) -> TileView {
    TileView v;
    SDL_GetWindowSize(render.window, &v.windowW, &v.windowH);
    v.size = TILE_SIZE * render.camera.zoom;
    return v;
}

auto tile_rect(const RenderState& render, const TileChunk& chunk, int lx, int ly, const TileView& v, SDL_FRect& out) -> bool {
    const float wx = ((static_cast<float>(chunk.cx) * CHUNK_SIZE) + static_cast<float>(lx) + 0.5F) * TILE_SIZE;
    const float wy = ((static_cast<float>(chunk.cy) * CHUNK_SIZE) + static_cast<float>(ly) + 0.5F) * TILE_SIZE;
    glm::vec2 s = render.camera.worldToScreen({wx, wy}, v.windowW, v.windowH);
    if (s.x < -v.size || s.y < -v.size || s.x > static_cast<float>(v.windowW) + v.size || s.y > static_cast<float>(v.windowH) + v.size) {
        return false;
    }
    out = {.x = s.x - (v.size * 0.5F), .y = s.y - (v.size * 0.5F), .w = v.size, .h = v.size};
    return true;
}

auto tile_texture(flecs::world world, SpriteCache& cache, const RenderState& render, const TileType& type) -> SDL_Texture* {
    if (type.texture == 0) {
        return nullptr;
    }
    if (auto cit = cache.textures.find(type.texture); cit != cache.textures.end()) {
        return cit->second;
    }
    if (const auto* store = world.try_get<AssetStore>()) {
        if (auto rit = store->ready.find(type.texture); rit != store->ready.end()) {
            SDL_Texture* tex = IMG_LoadTexture(render.target, rit->second.c_str());
            if (tex != nullptr) {
                cache.textures[type.texture] = tex;
            }
            return tex;
        }
    }
    return nullptr;
}

}

void Render::floor(flecs::iter& it, size_t, const RenderState& render, const TileChunk& chunk) {
    flecs::world world = it.world();
    const auto* tileset = world.try_get<Tileset>();
    if (tileset == nullptr) {
        return;
    }
    auto& cache = world.get_mut<SpriteCache>();
    const TileView v = view_of(render);
    for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            const uint16_t id = chunk.tiles[(ly * CHUNK_SIZE) + lx];
            if (id == TILE_EMPTY) {
                continue;
            }
            const TileType& type = tileset->type(id);
            if (type.solid) {
                continue;
            }
            SDL_FRect rect;
            if (!tile_rect(render, chunk, lx, ly, v, rect)) {
                continue;
            }
            SDL_Texture* tex = tile_texture(world, cache, render, type);
            if (tex != nullptr) {
                SDL_RenderTexture(render.target, tex, nullptr, &rect);
            } else {
                SDL_SetRenderDrawBlendMode(render.target, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(render.target, 60, 200, 255, 170);
                SDL_RenderFillRect(render.target, &rect);
            }
        }
    }
}

void Render::shadow(flecs::iter& it) {
    flecs::world world = it.world();
    auto* render = world.try_get_mut<RenderState>();
    const auto* tileset = world.try_get<Tileset>();
    if (render == nullptr || tileset == nullptr || render->occlusionTexture == nullptr) {
        return;
    }
    const TileView v = view_of(*render);
    if (v.windowW <= 0 || v.windowH <= 0) {
        return;
    }

    if (render->aoTexture == nullptr || render->aoW != v.windowW || render->aoH != v.windowH) {
        if (render->aoTexture != nullptr) {
            SDL_DestroyTexture(render->aoTexture);
        }
        render->aoTexture = SDL_CreateTexture(render->target, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, v.windowW, v.windowH);
        render->aoW = v.windowW;
        render->aoH = v.windowH;
        if (render->aoTexture != nullptr) {
            SDL_SetTextureBlendMode(render->aoTexture, SDL_BLENDMODE_BLEND);
        }
    }
    if (render->aoTexture == nullptr) {
        return;
    }

    const SDL_BlendMode max_alpha = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM);
    SDL_Texture* frame_target = SDL_GetRenderTarget(render->target);
    SDL_SetRenderTarget(render->target, render->aoTexture);
    SDL_SetRenderDrawColor(render->target, 0, 0, 0, 0);
    SDL_RenderClear(render->target);
    SDL_SetTextureBlendMode(render->occlusionTexture, max_alpha);

    const float ao = v.size * 1.5F;
    const float pad = (ao - v.size) * 0.5F;
    static flecs::query<const TileChunk> chunks = world.query<const TileChunk>();
    chunks.each([&](const TileChunk& chunk) -> void {
        for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                const uint16_t id = chunk.tiles[(ly * CHUNK_SIZE) + lx];
                if (id == TILE_EMPTY || !tileset->type(id).solid) {
                    continue;
                }
                SDL_FRect rect;
                if (!tile_rect(*render, chunk, lx, ly, v, rect)) {
                    continue;
                }
                SDL_FRect dst = {.x = rect.x - pad, .y = rect.y - pad, .w = ao, .h = ao};
                SDL_RenderTexture(render->target, render->occlusionTexture, nullptr, &dst);
            }
        }
    });

    SDL_SetTextureBlendMode(render->occlusionTexture, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(render->target, frame_target);
    SDL_RenderTexture(render->target, render->aoTexture, nullptr, nullptr);
}

void Render::walls_top(flecs::iter& it, size_t i, const RenderState& render, const TileChunk& chunk) {
    if (render.shadowSolid) {
        return;
    }
    Render::solid(it, i, render, chunk);
}

void Render::solid(flecs::iter& it, size_t, const RenderState& render, const TileChunk& chunk) {
    flecs::world world = it.world();
    const auto* tileset = world.try_get<Tileset>();
    if (tileset == nullptr) {
        return;
    }
    auto& cache = world.get_mut<SpriteCache>();
    const TileView v = view_of(render);
    for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            const uint16_t id = chunk.tiles[(ly * CHUNK_SIZE) + lx];
            if (id == TILE_EMPTY) {
                continue;
            }
            const TileType& type = tileset->type(id);
            if (!type.solid) {
                continue;
            }
            SDL_FRect rect;
            if (!tile_rect(render, chunk, lx, ly, v, rect)) {
                continue;
            }
            SDL_Texture* tex = tile_texture(world, cache, render, type);
            if (tex != nullptr) {
                SDL_RenderTexture(render.target, tex, nullptr, &rect);
            } else {
                SDL_SetRenderDrawBlendMode(render.target, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(render.target, 220, 70, 70, 255);
                SDL_RenderFillRect(render.target, &rect);
            }
        }
    }
}
