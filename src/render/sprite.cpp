#include "render.h"

#include <SDL3_image/SDL_image.h>

#include <cmath>

#include "component/asset.h"

static auto resolve_layer(flecs::world world, const RenderState& render, uint64_t hash) -> SDL_Texture* {
    if (hash == SPRITE_ENGINE_BASE) {
        return render.tankBaseTexture;
    }
    if (hash == SPRITE_ENGINE_TURRET) {
        return render.tankTurretTexture;
    }
    auto& cache = world.get_mut<SpriteCache>();
    auto cit = cache.textures.find(hash);
    if (cit != cache.textures.end()) {
        return cit->second;
    }
    if (const auto* store = world.try_get<AssetStore>()) {
        auto rit = store->ready.find(hash);
        if (rit != store->ready.end()) {
            SDL_Texture* tex = IMG_LoadTexture(render.target, rit->second.c_str());
            if (tex != nullptr) {
                cache.textures[hash] = tex;
                return tex;
            }
        }
    }
    return nullptr;
}

static auto sdl_blend(BlendMode mode) -> SDL_BlendMode {
    switch (mode) {
        case BlendMode::Add:
            return SDL_BLENDMODE_ADD;
        case BlendMode::Multiply:
            return SDL_BLENDMODE_MOD;
        case BlendMode::Modulate:
            return SDL_BLENDMODE_MUL;
        case BlendMode::Normal:
        default:
            return SDL_BLENDMODE_BLEND;
    }
}

void Render::sprite(flecs::iter& it, size_t, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col, const Blend* blend) {
    flecs::world world = it.world();

    int windowW;
    int windowH;
    SDL_GetWindowSize(render.window, &windowW, &windowH);
    glm::vec2 screenPos = render.camera.worldToScreen(pos.value, windowW, windowH);

    const float zoom = render.camera.zoom;
    const float screen_angle = rot.angle + render.camera.rotation;
    const float ca = std::cos(screen_angle);
    const float sa = std::sin(screen_angle);
    const auto cr = static_cast<Uint8>(col != nullptr ? col->value.r : 255.0F);
    const auto cg = static_cast<Uint8>(col != nullptr ? col->value.g : 255.0F);
    const auto cb = static_cast<Uint8>(col != nullptr ? col->value.b : 255.0F);
    const auto alpha = static_cast<Uint8>(std::clamp(blend != nullptr ? blend->opacity : 1.0F, 0.0F, 1.0F) * 255.0F);
    const SDL_BlendMode mode = sdl_blend(blend != nullptr ? blend->mode : BlendMode::Normal);

    for (int layer = 0; layer < SPRITE_LAYERS; ++layer) {
        const uint64_t hash = sprite.texture[layer];
        if (hash == 0) {
            continue;
        }
        SDL_Texture* tex = resolve_layer(world, render, hash);
        if (tex == nullptr) {
            continue;
        }

        float texW;
        float texH;
        SDL_GetTextureSize(tex, &texW, &texH);
        const float w = texW * 0.5F * zoom;
        const float h = texH * 0.5F * zoom;

        const float ox = sprite.offset_x[layer];
        const float oy = sprite.offset_y[layer];
        const glm::vec2 off = {((ox * ca) - (oy * sa)) * zoom, ((ox * sa) + (oy * ca)) * zoom};
        SDL_FPoint pivot = {.x = (0.5F + sprite.pivot_x[layer]) * w, .y = (0.5F + sprite.pivot_y[layer]) * h};
        SDL_FRect rect = {.x = screenPos.x + off.x - pivot.x, .y = screenPos.y + off.y - pivot.y, .w = w, .h = h};

        SDL_SetTextureColorMod(tex, cr, cg, cb);
        SDL_SetTextureAlphaMod(tex, alpha);
        SDL_SetTextureBlendMode(tex, mode);
        SDL_RenderTextureRotated(render.target, tex, nullptr, &rect, glm::degrees(screen_angle), &pivot, SDL_FLIP_NONE);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
}

void Render::prop_under(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col, const Blend* blend, const Layer* layer) {
    if (layer != nullptr && layer->value == RenderLayer::Underlay) {
        Render::sprite(it, i, render, pos, rot, sprite, col, blend);
    }
}

void Render::prop_below(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col, const Blend* blend, const Layer* layer) {
    if (layer == nullptr || layer->value == RenderLayer::Ground) {
        Render::sprite(it, i, render, pos, rot, sprite, col, blend);
    }
}

void Render::prop_above(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col, const Blend* blend, const Layer* layer) {
    if (layer != nullptr && layer->value == RenderLayer::Overlay) {
        Render::sprite(it, i, render, pos, rot, sprite, col, blend);
    }
}

void Render::overhead_top(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col, const Blend* blend, const Layer* layer) {
    if (!render.shadowSolid && layer != nullptr && layer->value == RenderLayer::Overlay) {
        Render::sprite(it, i, render, pos, rot, sprite, col, blend);
    }
}
