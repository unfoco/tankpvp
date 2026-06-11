#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_clay.h>
#include <clay.h>
#include <flecs.h>

#include <cmath>

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>

#include "component/asset.h"
#include "component/effect.h"
#include "component/interface.h"
#include "component/object.h"
#include "component/render.h"
#include "component/world.h"

constexpr auto WIDTH = 1280;
constexpr auto HEIGHT = 720;

struct Viewport {
    glm::vec2 position{0};
    float zoom = 1.0F;
    float rotation = 0.0F;
    glm::vec2 offset{0};
    glm::vec2 shakeOffset{0};

    [[nodiscard]] auto worldToScreen(const glm::vec2& worldPos, int windowW, int windowH) const -> glm::vec2 {
        glm::vec2 d = (worldPos - position) * zoom;
        if (rotation != 0.0F) {
            float c = std::cos(rotation);
            float s = std::sin(rotation);
            d = {(d.x * c) - (d.y * s), (d.x * s) + (d.y * c)};
        }
        return {d.x + (static_cast<float>(windowW) / 2.0F) + offset.x + shakeOffset.x, d.y + (static_cast<float>(windowH) / 2.0F) + offset.y + shakeOffset.y};
    }

    [[nodiscard]] auto screenToWorld(const glm::vec2& screenPos, int windowW, int windowH) const -> glm::vec2 {
        glm::vec2 d = {screenPos.x - (static_cast<float>(windowW) / 2.0F) - offset.x - shakeOffset.x, screenPos.y - (static_cast<float>(windowH) / 2.0F) - offset.y - shakeOffset.y};
        if (rotation != 0.0F) {
            float c = std::cos(-rotation);
            float s = std::sin(-rotation);
            d = {(d.x * c) - (d.y * s), (d.x * s) + (d.y * c)};
        }
        return (d / zoom) + position;
    }
};

struct RenderState {
    SDL_Window* window;
    SDL_Renderer* target;

    SDL_Clay_RendererData clay;

    Viewport camera;

    SDL_Texture* tankBaseTexture;
    SDL_Texture* tankTurretTexture;
    SDL_Texture* weaponBulletTexture;
    SDL_Texture* smokeTexture = nullptr;
    SDL_Texture* occlusionTexture = nullptr;
    SDL_Texture* aoTexture = nullptr;
    SDL_Texture* radarTexture = nullptr;
    int aoW = 0;
    int aoH = 0;
    SDL_Texture* fragmentTextures[10] = {};
    int fragmentCount = 0;

    SDL_Texture* frameA = nullptr;
    SDL_Texture* frameB = nullptr;
    SDL_Texture* snapshot = nullptr;
    int frameW = 0;
    int frameH = 0;
    bool curIsA = true;
    double lastStart = -1;
    float shakeTime = 0;
    bool shadowSolid = false;
    SDL_Texture* lightTexture = nullptr;
    SDL_Texture* lightBlur = nullptr;
    SDL_Texture* maskTexture = nullptr;
    SDL_Texture* entityTexture = nullptr;
    SDL_Texture* effectsTexture = nullptr;
    SDL_Texture* effectsHalf = nullptr;
    SDL_Texture* vignetteTexture = nullptr;
};

struct RenderPipeline {
    flecs::entity_t value = 0;
};

struct SpriteCache {
    std::unordered_map<uint64_t, SDL_Texture*> textures;
};

struct Particle {
    glm::vec2 pos{0};
    glm::vec2 vel{0};
    float angle = 0;
    float spin = 0;
    float life = 0;
    float max_life = 1;
    float size = 16;
    int fragment = 0;
    Uint8 r = 255, g = 255, b = 255;
    bool smoke = false;
    uint64_t texture = 0;
    bool additive = false;
    float alpha = 1.0F;
    float grow = 0;
    float gravity = 0;
    float drag = 3.0F;
};

struct Render {
    Render(flecs::world& world);

   private:
    static void init(flecs::iter& it, size_t i);

    static void start(flecs::iter& it, size_t i, RenderState& render);
    static void finish(flecs::iter& it, size_t i, RenderState& render);

    static void interface(flecs::iter& it, size_t i, const RenderState& render, InterfaceCommands& commands);
    static void camera(flecs::iter& it, size_t i, RenderState& render, const Position& pos);

    static void bullet(flecs::iter& it, size_t i, RenderState& render, const Position& pos);
    static void sprite(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col, const Blend* blend);
    static void prop_under(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col, const Blend* blend, const Layer* layer);
    static void prop_below(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col, const Blend* blend, const Layer* layer);
    static void prop_above(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col, const Blend* blend, const Layer* layer);
    static void floor(flecs::iter& it, size_t i, const RenderState& render, const TileChunk& chunk);
    static void shadow(flecs::iter& it);
    static void solid(flecs::iter& it, size_t i, const RenderState& render, const TileChunk& chunk);
    static void walls_top(flecs::iter& it, size_t i, const RenderState& render, const TileChunk& chunk);
    static void overhead_top(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col, const Blend* blend, const Layer* layer);

    static void burst(flecs::entity e, const RequestEffect& req);
    static void emit(flecs::entity e, const RequestParticles& req);
    static void age(flecs::iter& it, size_t i, Particle& p);
    static void particles(flecs::iter& it, size_t i, const RenderState& render, const Particle& p);

    static void smoke(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const VisionBlocker& vb);
    static void radar(flecs::iter& it, size_t i, RenderState& render);
    static void entities_begin(flecs::iter& it, size_t i, RenderState& render);
    static void entities_end(flecs::iter& it, size_t i, RenderState& render);
    static void postprocess(flecs::iter& it, size_t i, RenderState& render);
    static auto effects(RenderState& render, SDL_Texture* cur, const Camera& cam, int w, int h) -> SDL_Texture*;
};
