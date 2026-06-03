#include "render.h"

void Render::tank(flecs::iter& it, size_t, const RenderState& render, const Color& col, const Position& pos, const Rotation& rot) {
    SDL_Texture* baseTex = render.tankBaseTexture;
    SDL_Texture* turretTex = render.tankTurretTexture;

    int windowW, windowH;
    SDL_GetWindowSize(render.window, &windowW, &windowH);
    glm::vec2 screenPos = render.camera.worldToScreen(pos.value, windowW, windowH);

    float baseW, baseH;
    SDL_GetTextureSize(baseTex, &baseW, &baseH);
    baseW *= 0.5f * render.camera.zoom;
    baseH *= 0.5f * render.camera.zoom;
    SDL_FRect baseRect = {
        screenPos.x - (baseW / 2.0f),
        screenPos.y - (baseH / 2.0f),
        baseW,
        baseH
    };
    SDL_FPoint basePivot = { baseW / 2.0f, baseH / 2.0f };
    SDL_SetTextureColorMod(baseTex, col.value.r, col.value.g, col.value.b);
    SDL_RenderTextureRotated(render.target, baseTex, nullptr, &baseRect, glm::degrees(rot.angle), &basePivot, SDL_FLIP_NONE);
    SDL_SetTextureColorMod(baseTex, 255, 255, 255);

    float turretW, turretH;
    SDL_GetTextureSize(turretTex, &turretW, &turretH);
    turretW *= 0.5f * render.camera.zoom;
    turretH *= 0.5f * render.camera.zoom;
    SDL_FPoint turretPivot = {
        turretW * 0.25f,
        turretH / 2.0f,
    };
    SDL_FRect turretRect = {
        screenPos.x - turretPivot.x,
        screenPos.y - turretPivot.y,
        turretW,
        turretH
    };
    SDL_SetTextureColorMod(turretTex, col.value.r, col.value.g, col.value.b);
    SDL_RenderTextureRotated(render.target, turretTex, nullptr, &turretRect, glm::degrees(rot.angle), &turretPivot, SDL_FLIP_NONE);
    SDL_SetTextureColorMod(turretTex, 255, 255, 255);
}
