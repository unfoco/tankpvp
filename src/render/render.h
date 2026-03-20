#pragma once

#include <SDL3/SDL_clay.h>
#include <SDL3/SDL.h>

#include <glm/glm.hpp>

#include <clay.h>
#include <flecs.h>

#include "component/interface.h"
#include "component/object.h"

constexpr auto WIDTH = 1280;
constexpr auto HEIGHT = 720;

struct Camera {
    glm::vec2 position;
    float zoom;

    glm::vec2 worldToScreen(const glm::vec2& worldPos, int windowW, int windowH) const {
        return glm::vec2(
            (worldPos.x - position.x) * zoom + (windowW / 2.0f),
            (worldPos.y - position.y) * zoom + (windowH / 2.0f)
        );
    }

    glm::vec2 screenToWorld(const glm::vec2& screenPos, int windowW, int windowH) const {
        return glm::vec2(
            (screenPos.x - (windowW / 2.0f)) / zoom + position.x,
            (screenPos.y - (windowH / 2.0f)) / zoom + position.y
        );
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
};

struct Render {
    Render(flecs::world&);

private:
    static void init(flecs::iter&, size_t);

    static void start(flecs::iter&, size_t, const RenderState&);
    static void finish(flecs::iter&, size_t, const RenderState&);

    static void interface(flecs::iter&, size_t, const RenderState&, InterfaceCommands&);
    static void camera(flecs::iter&, size_t, RenderState&, const Position&);

    static void bullet(flecs::iter&, size_t, RenderState&, const Position&);
    static void tank(flecs::iter&, size_t, const RenderState&, const Color&, const Position&, const Rotation&);
};
