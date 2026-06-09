#include "render.h"

#include <SDL3_image/SDL_image.h>

#include "component/world.h"

void Render::tiles(flecs::iter& it, size_t, const RenderState& render, const TileChunk& chunk) {
    flecs::world world = it.world();
    const auto* tileset = world.try_get<Tileset>();
    if (tileset == nullptr) {
        return;
    }
    auto& cache = world.get_mut<SpriteCache>();

    int windowW;
    int windowH;
    SDL_GetWindowSize(render.window, &windowW, &windowH);
    const float zoom = render.camera.zoom;
    const float size = TILE_SIZE * zoom;

    for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            const uint16_t id = chunk.tiles[(ly * CHUNK_SIZE) + lx];
            if (id == TILE_EMPTY) {
                continue;
            }
            const TileType& type = tileset->type(id);

            const float wx = ((static_cast<float>(chunk.cx) * CHUNK_SIZE) + static_cast<float>(lx) + 0.5F) * TILE_SIZE;
            const float wy = ((static_cast<float>(chunk.cy) * CHUNK_SIZE) + static_cast<float>(ly) + 0.5F) * TILE_SIZE;
            glm::vec2 screen = render.camera.worldToScreen({wx, wy}, windowW, windowH);
            if (screen.x < -size || screen.y < -size || screen.x > static_cast<float>(windowW) + size || screen.y > static_cast<float>(windowH) + size) {
                continue;
            }
            SDL_FRect rect = {.x = screen.x - (size * 0.5F), .y = screen.y - (size * 0.5F), .w = size, .h = size};

            SDL_Texture* tex = nullptr;
            if (type.texture != 0) {
                if (auto cit = cache.textures.find(type.texture); cit != cache.textures.end()) {
                    tex = cit->second;
                } else if (const auto* store = world.try_get<AssetStore>()) {
                    if (auto rit = store->ready.find(type.texture); rit != store->ready.end()) {
                        tex = IMG_LoadTexture(render.target, rit->second.c_str());
                        if (tex != nullptr) {
                            cache.textures[type.texture] = tex;
                        }
                    }
                }
            }

            if (tex != nullptr) {
                SDL_RenderTexture(render.target, tex, nullptr, &rect);
            } else if (type.solid) {
                SDL_SetRenderDrawBlendMode(render.target, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(render.target, 220, 70, 70, 255);
                SDL_RenderFillRect(render.target, &rect);
            } else {
                SDL_SetRenderDrawBlendMode(render.target, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(render.target, 60, 200, 255, 170);
                SDL_RenderFillRect(render.target, &rect);
            }
        }
    }
}
