#include <algorithm>

#include "render.h"

void Render::smoke(flecs::iter& /*it*/, size_t /*i*/, const RenderState& render, const Position& pos, const VisionBlocker& vb) {
    SDL_Texture* tex = render.smokeTexture;
    if (tex == nullptr) {
        return;
    }
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(render.window, &w, &h);
    glm::vec2 sp = render.camera.worldToScreen(pos.value, w, h);
    float size = vb.radius * 2.0F * render.camera.zoom;
    SDL_FRect rect = {.x = sp.x - (size / 2.0F), .y = sp.y - (size / 2.0F), .w = size, .h = size};
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(tex, static_cast<Uint8>(vb.r), static_cast<Uint8>(vb.g), static_cast<Uint8>(vb.b));
    Uint8 a = static_cast<Uint8>(std::clamp(vb.alpha, 0.0F, 1.0F) * 235.0F);
    SDL_SetTextureAlphaMod(tex, a);
    for (int s = 0; s < 5; ++s) {
        SDL_RenderTexture(render.target, tex, nullptr, &rect);
    }
    SDL_FRect core = {.x = sp.x - (size * 0.36F), .y = sp.y - (size * 0.36F), .w = size * 0.72F, .h = size * 0.72F};
    for (int s = 0; s < 4; ++s) {
        SDL_RenderTexture(render.target, tex, nullptr, &core);
    }
    SDL_SetTextureAlphaMod(tex, 255);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
}
