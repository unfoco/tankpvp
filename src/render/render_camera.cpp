#include <algorithm>

#include "render.h"

void Render::camera(flecs::iter& it, size_t, RenderState& render, const Position& pos) {
    int width, height;
    SDL_GetWindowSize(render.window, &width, &height);

    render.camera.zoom = std::min(
        static_cast<float>(width) / WIDTH,
        static_cast<float>(height) / HEIGHT
    );

    render.camera.position = pos.value;
}
