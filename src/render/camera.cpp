#include <algorithm>
#include <cmath>

#include "render.h"

static auto ease(float cur, float target, float rate, float dt) -> float {
    return cur + ((target - cur) * (1.0F - std::exp(-rate * dt)));
}

void Render::camera(flecs::iter& it, size_t i, RenderState& render, const Position& pos) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(render.window, &width, &height);
    float base_zoom = std::min(static_cast<float>(width) / WIDTH, static_cast<float>(height) / HEIGHT);
    float dt = it.delta_time();
    if (dt <= 0.0F || dt > 0.1F) {
        dt = 1.0F / 60.0F;
    }

    flecs::entity self = it.entity(i);
    auto* cam = self.try_get_mut<Camera>();

    glm::vec2 focus = pos.value;
    float zoom = base_zoom;
    float rotation = 0.0F;
    glm::vec2 offset{0};
    float follow = 0.16F;
    if (cam != nullptr) {
        if (cam->focus_x != 0.0F || cam->focus_y != 0.0F) {
            focus = {cam->focus_x, cam->focus_y};
        }
        zoom = base_zoom * (cam->zoom > 0.0F ? cam->zoom : 1.0F);
        rotation = cam->rotation;
        offset = {cam->offset_x, cam->offset_y};
        follow = cam->follow > 0.0F ? cam->follow : 0.16F;
    }
    render.shadowSolid = (cam != nullptr && cam->shadow_solid > 0.5F);

    glm::vec2 delta = focus - render.camera.position;
    if (glm::dot(delta, delta) > 600.0F * 600.0F) {
        render.camera.position = focus;
    } else {
        render.camera.position += delta * (1.0F - std::exp(-(follow * 60.0F) * dt));
    }
    render.camera.zoom = ease(render.camera.zoom, zoom, 9.0F, dt);
    render.camera.rotation = ease(render.camera.rotation, rotation, 9.0F, dt);
    render.camera.offset.x = ease(render.camera.offset.x, offset.x, 12.0F, dt);
    render.camera.offset.y = ease(render.camera.offset.y, offset.y, 12.0F, dt);

    if (cam != nullptr && cam->shake > 0.0F) {
        float amp = cam->shake * cam->shake * 24.0F;
        render.shakeTime += dt * 38.0F;
        render.camera.shakeOffset = {std::sin(render.shakeTime * 1.3F) * amp, std::cos(render.shakeTime * 1.9F) * amp};
        cam->shake = std::max(0.0F, cam->shake - (dt * 1.6F));
    } else {
        render.camera.shakeOffset = {0, 0};
    }
    if (cam != nullptr && cam->flash > 0.0F) {
        cam->flash = std::max(0.0F, cam->flash - ((cam->flash_fade > 0.0F ? cam->flash_fade : 1.0F) * dt));
    }
}
