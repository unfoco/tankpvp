#include "render.h"

void Render::tank(flecs::iter& it, size_t i, const RenderState& render, const Color& col, const Position& pos, const Rotation& rot) {
    SDL_Texture* baseTex = render.tankBaseTexture;
    SDL_Texture* turretTex = render.tankTurretTexture;

    int windowW;
    int windowH;
    SDL_GetWindowSize(render.window, &windowW, &windowH);
    glm::vec2 screenPos = render.camera.worldToScreen(pos.value, windowW, windowH);

    float baseW;
    float baseH;
    SDL_GetTextureSize(baseTex, &baseW, &baseH);
    baseW *= 0.5F * render.camera.zoom;
    baseH *= 0.5F * render.camera.zoom;
    SDL_FRect baseRect = {.x = screenPos.x - (baseW / 2.0F), .y = screenPos.y - (baseH / 2.0F), .w = baseW, .h = baseH};
    SDL_FPoint basePivot = {.x = baseW / 2.0F, .y = baseH / 2.0F};
    SDL_SetTextureColorMod(baseTex, static_cast<Uint8>(col.value.r), static_cast<Uint8>(col.value.g), static_cast<Uint8>(col.value.b));
    SDL_RenderTextureRotated(render.target, baseTex, nullptr, &baseRect, glm::degrees(rot.angle), &basePivot, SDL_FLIP_NONE);
    SDL_SetTextureColorMod(baseTex, 255, 255, 255);

    float turretW;
    float turretH;
    SDL_GetTextureSize(turretTex, &turretW, &turretH);
    turretW *= 0.5F * render.camera.zoom;
    turretH *= 0.5F * render.camera.zoom;
    SDL_FPoint turretPivot = {
        .x = turretW * 0.25F,
        .y = turretH / 2.0F,
    };
    SDL_FRect turretRect = {.x = screenPos.x - turretPivot.x, .y = screenPos.y - turretPivot.y, .w = turretW, .h = turretH};
    SDL_SetTextureColorMod(turretTex, static_cast<Uint8>(col.value.r), static_cast<Uint8>(col.value.g), static_cast<Uint8>(col.value.b));
    SDL_RenderTextureRotated(render.target, turretTex, nullptr, &turretRect, glm::degrees(rot.angle), &turretPivot, SDL_FLIP_NONE);
    SDL_SetTextureColorMod(turretTex, 255, 255, 255);
}
