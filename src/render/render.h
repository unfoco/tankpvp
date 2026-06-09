#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_clay.h>
#include <clay.h>
#include <flecs.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>

#include "component/asset.h"
#include "component/interface.h"
#include "component/object.h"
#include "component/world.h"

constexpr auto WIDTH = 1280;
constexpr auto HEIGHT = 720;

struct Camera {
    glm::vec2 position;
    float zoom;

    [[nodiscard]] auto worldToScreen(const glm::vec2& worldPos, int windowW, int windowH) const -> glm::vec2 {
        return {((worldPos.x - position.x) * zoom) + (static_cast<float>(windowW) / 2.0F), ((worldPos.y - position.y) * zoom) + (static_cast<float>(windowH) / 2.0F)};
    }

    [[nodiscard]] auto screenToWorld(const glm::vec2& screenPos, int windowW, int windowH) const -> glm::vec2 {
        return {((screenPos.x - (static_cast<float>(windowW) / 2.0F)) / zoom) + position.x, ((screenPos.y - (static_cast<float>(windowH) / 2.0F)) / zoom) + position.y};
    }
};

struct RenderState {
    SDL_Window* window;
    SDL_Renderer* target;

    SDL_Clay_RendererData clay;

    Camera camera;

    SDL_Texture* tankBaseTexture;
    SDL_Texture* tankTurretTexture;
    SDL_Texture* weaponBulletTexture;

    SDL_Texture* frameA = nullptr;
    SDL_Texture* frameB = nullptr;
    SDL_Texture* snapshot = nullptr;
    int frameW = 0;
    int frameH = 0;
    bool curIsA = true;
    double lastStart = -1;
};

struct RenderPipeline {
    flecs::entity_t value = 0;
};

struct SpriteCache {
    std::unordered_map<uint64_t, SDL_Texture*> textures;
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
    static void sprite(flecs::iter& it, size_t i, const RenderState& render, const Position& pos, const Rotation& rot, const Sprite& sprite, const Color* col);
    static void tiles(flecs::iter& it, size_t i, const RenderState& render, const TileChunk& chunk);
};
