#include "render.h"

void Render::bullet(flecs::iter& it, size_t, RenderState& render, const Position& pos) {
    SDL_Texture* bulletTex = render.weaponBulletTexture;

    int windowW, windowH;
    SDL_GetWindowSize(render.window, &windowW, &windowH);
    glm::vec2 screenPos = render.camera.worldToScreen(pos.value, windowW, windowH);

    float baseW, baseH;
    SDL_GetTextureSize(bulletTex, &baseW, &baseH);
    baseW *= 0.5f * render.camera.zoom;
    baseH *= 0.5f * render.camera.zoom;
    SDL_FRect baseRect = {
        screenPos.x - (baseW / 2.0f),
        screenPos.y - (baseH / 2.0f),
        baseW,
        baseH
    };
    SDL_FPoint basePivot = { baseW / 2.0f, baseH / 2.0f };
    SDL_RenderTexture(render.target, bulletTex, nullptr, &baseRect);
    SDL_SetTextureColorMod(bulletTex, 255, 255, 255);
}
