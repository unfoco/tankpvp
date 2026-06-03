#include "render.h"

void Render::bullet(flecs::iter& it, size_t i, RenderState& render, const Position& pos) {
    SDL_Texture* bulletTex = render.weaponBulletTexture;

    int windowW;
    int windowH;
    SDL_GetWindowSize(render.window, &windowW, &windowH);
    glm::vec2 screenPos = render.camera.worldToScreen(pos.value, windowW, windowH);

    float baseW;
    float baseH;
    SDL_GetTextureSize(bulletTex, &baseW, &baseH);
    baseW *= 0.5F * render.camera.zoom;
    baseH *= 0.5F * render.camera.zoom;
    SDL_FRect baseRect = {.x = screenPos.x - (baseW / 2.0F), .y = screenPos.y - (baseH / 2.0F), .w = baseW, .h = baseH};
    SDL_FPoint basePivot = {.x = baseW / 2.0F, .y = baseH / 2.0F};
    SDL_RenderTexture(render.target, bulletTex, nullptr, &baseRect);
    SDL_SetTextureColorMod(bulletTex, 255, 255, 255);
}
