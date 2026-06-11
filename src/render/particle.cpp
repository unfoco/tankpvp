#include "render.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "component/asset.h"

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

void Render::emit(flecs::entity e, const RequestParticles& req) {
    flecs::world world = e.world();
    if (world.try_get<RenderState>() == nullptr) {
        return;
    }
    int count = std::clamp(static_cast<int>(req.count), 1, 512);
    for (int i = 0; i < count; ++i) {
        float ang = req.dir + ((rnd() - 0.5F) * req.spread);
        float speed = req.speed_min + (rnd() * std::max(0.0F, req.speed_max - req.speed_min));
        float size = req.size_min + (rnd() * std::max(0.0F, req.size_max - req.size_min));
        float life = req.life_min + (rnd() * std::max(0.0F, req.life_max - req.life_min));
        world.entity().set(Particle{
            .pos = req.position + glm::vec2{(rnd() - 0.5F) * 8.0F, (rnd() - 0.5F) * 8.0F},
            .vel = {std::cos(ang) * speed, std::sin(ang) * speed},
            .angle = rnd() * 6.2832F,
            .spin = (rnd() - 0.5F) * 2.0F * req.spin,
            .life = life,
            .max_life = life,
            .size = size,
            .fragment = -1,
            .r = req.r,
            .g = req.g,
            .b = req.b,
            .smoke = req.texture == 1,
            .texture = req.texture > 1 ? req.texture : 0,
            .additive = req.additive,
            .alpha = static_cast<float>(req.alpha) / 255.0F,
            .grow = req.grow,
            .gravity = req.gravity,
            .drag = req.drag,
        });
    }
}

void Render::age(flecs::iter& it, size_t i, Particle& p) {
    float dt = it.delta_time();
    p.pos += p.vel * dt;
    if (p.fragment >= 0) {
        if (p.smoke) {
            p.size += 26.0F * dt;
            p.vel *= std::max(0.0F, 1.0F - (0.9F * dt));
        } else {
            p.vel *= std::max(0.0F, 1.0F - (3.0F * dt));
            p.angle += p.spin * dt;
        }
    } else {
        p.vel.y += p.gravity * dt;
        p.vel *= std::max(0.0F, 1.0F - (p.drag * dt));
        p.angle += p.spin * dt;
        p.size = std::max(1.0F, p.size + (p.grow * dt));
    }
    p.life -= dt;
    if (p.life <= 0.0F) {
        it.entity(i).destruct();
    }
}

static auto particle_texture(flecs::world world, const RenderState& render, uint64_t hash) -> SDL_Texture* {
    auto& cache = world.get_mut<SpriteCache>();
    if (auto cit = cache.textures.find(hash); cit != cache.textures.end()) {
        return cit->second;
    }
    if (const auto* store = world.try_get<AssetStore>()) {
        if (auto rit = store->ready.find(hash); rit != store->ready.end()) {
            SDL_Texture* tex = IMG_LoadTexture(render.target, rit->second.c_str());
            if (tex != nullptr) {
                cache.textures[hash] = tex;
                return tex;
            }
        }
    }
    return nullptr;
}

void Render::particles(flecs::iter& it, size_t, const RenderState& render, const Particle& p) {
    int windowW = 0;
    int windowH = 0;
    SDL_GetWindowSize(render.window, &windowW, &windowH);
    glm::vec2 screen = render.camera.worldToScreen(p.pos, windowW, windowH);
    float fade = std::clamp(p.life / p.max_life, 0.0F, 1.0F) * std::clamp(p.alpha, 0.0F, 1.0F);

    SDL_Texture* tex = nullptr;
    bool soft = false;
    if (p.texture != 0) {
        tex = particle_texture(it.world(), render, p.texture);
    } else if (p.smoke) {
        tex = render.smokeTexture;
        soft = true;
    } else if (p.fragment >= 0 && p.fragment < render.fragmentCount) {
        tex = render.fragmentTextures[p.fragment];
    }
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
    SDL_SetTextureBlendMode(tex, p.additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(tex, p.r, p.g, p.b);
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(fade * (soft ? 110.0F : 255.0F)));
    if (p.angle != 0.0F && !soft) {
        SDL_RenderTextureRotated(render.target, tex, nullptr, &dst, glm::degrees(p.angle + render.camera.rotation), &pivot, SDL_FLIP_NONE);
    } else {
        SDL_RenderTexture(render.target, tex, nullptr, &dst);
    }
    SDL_SetTextureAlphaMod(tex, 255);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
}
