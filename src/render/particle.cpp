#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
auto rnd() -> float {
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}
}

void Render::burst(flecs::entity e, const RequestEffect& req) {
    flecs::world world = e.world();
    const auto* rs = world.try_get<RenderState>();
    int nfrag = (rs != nullptr && rs->fragmentCount > 0) ? rs->fragmentCount : 1;

    int shards = 7 + (std::rand() % 4);
    for (int i = 0; i < shards; ++i) {
        float ang = rnd() * 6.2832F;
        float speed = 70.0F + (rnd() * 150.0F);
        float size = 7.0F + (rnd() * 13.0F);
        float life = 0.45F + (rnd() * 0.45F);
        world.entity().set(Particle{
            .pos = req.position + glm::vec2{(rnd() - 0.5F) * 12.0F, (rnd() - 0.5F) * 12.0F},
            .vel = {std::cos(ang) * speed, std::sin(ang) * speed},
            .angle = rnd() * 6.2832F,
            .spin = (rnd() - 0.5F) * 18.0F,
            .life = life,
            .max_life = life,
            .size = size,
            .fragment = std::rand() % nfrag,
            .r = req.r,
            .g = req.g,
            .b = req.b,
            .smoke = false,
        });
    }

    int puffs = 9 + (std::rand() % 5);
    for (int i = 0; i < puffs; ++i) {
        float ang = rnd() * 6.2832F;
        float dist = rnd() * 16.0F;
        float speed = 6.0F + (rnd() * 22.0F);
        float size = 16.0F + (rnd() * 28.0F);
        float life = 0.7F + (rnd() * 0.8F);
        auto gray = static_cast<Uint8>(45 + (std::rand() % 55));
        world.entity().set(Particle{
            .pos = req.position + glm::vec2{std::cos(ang) * dist, std::sin(ang) * dist},
            .vel = {std::cos(ang) * speed * 0.5F, std::sin(ang) * speed * 0.5F},
            .angle = 0,
            .spin = 0,
            .life = life,
            .max_life = life,
            .size = size,
            .r = gray,
            .g = gray,
            .b = static_cast<Uint8>(gray + 6),
            .smoke = true,
        });
    }

    e.destruct();
}

void Render::age(flecs::iter& it, size_t i, Particle& p) {
    float dt = it.delta_time();
    p.pos += p.vel * dt;
    if (p.smoke) {
        p.size += 26.0F * dt;
        p.vel *= std::max(0.0F, 1.0F - (0.9F * dt));
    } else {
        p.vel *= std::max(0.0F, 1.0F - (3.0F * dt));
        p.angle += p.spin * dt;
    }
    p.life -= dt;
    if (p.life <= 0.0F) {
        it.entity(i).destruct();
    }
}

void Render::particles(flecs::iter& it, size_t, const RenderState& render, const Particle& p) {
    (void)it;
    int windowW = 0;
    int windowH = 0;
    SDL_GetWindowSize(render.window, &windowW, &windowH);
    glm::vec2 screen = render.camera.worldToScreen(p.pos, windowW, windowH);
    float fade = std::clamp(p.life / p.max_life, 0.0F, 1.0F);

    if (p.smoke) {
        SDL_Texture* tex = render.smokeTexture;
        if (tex == nullptr) {
            return;
        }
        float sz = p.size * render.camera.zoom;
        SDL_FRect dst = {.x = screen.x - (sz / 2.0F), .y = screen.y - (sz / 2.0F), .w = sz, .h = sz};
        SDL_SetTextureColorMod(tex, p.r, p.g, p.b);
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(fade * 110.0F));
        SDL_RenderTexture(render.target, tex, nullptr, &dst);
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        return;
    }

    SDL_Texture* tex = (p.fragment >= 0 && p.fragment < render.fragmentCount) ? render.fragmentTextures[p.fragment] : nullptr;
    if (tex == nullptr) {
        return;
    }
    float texW = 0;
    float texH = 0;
    SDL_GetTextureSize(tex, &texW, &texH);
    if (texW <= 0.0F || texH <= 0.0F) {
        return;
    }
    float scale = (p.size / std::max(texW, texH)) * render.camera.zoom;
    float w = texW * scale;
    float h = texH * scale;
    SDL_FRect dst = {.x = screen.x - (w / 2.0F), .y = screen.y - (h / 2.0F), .w = w, .h = h};
    SDL_FPoint pivot = {.x = w / 2.0F, .y = h / 2.0F};
    SDL_SetTextureColorMod(tex, p.r, p.g, p.b);
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(fade * 255.0F));
    SDL_RenderTextureRotated(render.target, tex, nullptr, &dst, glm::degrees(p.angle), &pivot, SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(tex, 255);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
}
