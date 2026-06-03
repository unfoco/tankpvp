#include <algorithm>

#include "render.h"

void Render::camera(flecs::iter& it, size_t i, RenderState& render, const Position& pos) {
    int width;
    int height;
    SDL_GetWindowSize(render.window, &width, &height);

    render.camera.zoom = std::min(static_cast<float>(width) / WIDTH, static_cast<float>(height) / HEIGHT);

    glm::vec2 delta = pos.value - render.camera.position;
    if (glm::dot(delta, delta) > 300.0F * 300.0F) {
        render.camera.position = pos.value;
    } else {
        render.camera.position += delta * 0.25F;
    }
}
