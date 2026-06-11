#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "component/network.h"
#include "component/world.h"
#include "render.h"
#include "util/ballistics.h"

static auto multiply_blend() -> SDL_BlendMode {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_COLOR, SDL_BLENDOPERATION_ADD,
                                      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
}
static auto add_rgba() -> SDL_BlendMode {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                      SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
}
static auto accum_blend() -> SDL_BlendMode {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                      SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
}
static auto mask_mul() -> SDL_BlendMode {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_COLOR, SDL_BLENDOPERATION_ADD,
                                      SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
}
static auto premult_blend() -> SDL_BlendMode {
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                      SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
}

static void fullscreen(SDL_Renderer* r, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 a) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, cr, cg, cb, a);
    SDL_FRect full = {.x = 0, .y = 0, .w = static_cast<float>(w), .h = static_cast<float>(h)};
    SDL_RenderFillRect(r, &full);
}

static std::vector<SDL_Vertex> g_vis_verts;
static std::vector<int> g_vis_idx;

static auto cast_ray(const WorldGrid& grid, const Tileset& tileset, glm::vec2 o, glm::vec2 dir, float range) -> float {
    auto solid_cell = [&](int cx, int cy) -> bool {
        return ballistics::solid(ballistics::tile_at(grid, tileset, (static_cast<float>(cx) + 0.5F) * TILE_SIZE, (static_cast<float>(cy) + 0.5F) * TILE_SIZE));
    };
    int tx = static_cast<int>(std::floor(o.x / TILE_SIZE));
    int ty = static_cast<int>(std::floor(o.y / TILE_SIZE));
    int sx = dir.x > 0.0F ? 1 : -1;
    int sy = dir.y > 0.0F ? 1 : -1;
    constexpr float INF = 1e30F;
    float tdx = dir.x != 0.0F ? TILE_SIZE / std::abs(dir.x) : INF;
    float tdy = dir.y != 0.0F ? TILE_SIZE / std::abs(dir.y) : INF;
    float bx = (static_cast<float>(tx) + (sx > 0 ? 1.0F : 0.0F)) * TILE_SIZE;
    float by = (static_cast<float>(ty) + (sy > 0 ? 1.0F : 0.0F)) * TILE_SIZE;
    float tmx = dir.x != 0.0F ? (bx - o.x) / dir.x : INF;
    float tmy = dir.y != 0.0F ? (by - o.y) / dir.y : INF;

    float hit = -1.0F;
    float t = 0.0F;
    for (int i = 0; i < 256 && t < range + TILE_SIZE; ++i) {
        if (tmx < tmy) {
            t = tmx;
            tmx += tdx;
            tx += sx;
        } else {
            t = tmy;
            tmy += tdy;
            ty += sy;
        }
        bool solid = solid_cell(tx, ty);
        if (hit < 0.0F) {
            if (solid && t < range) {
                hit = t;
            }
        } else if (!solid) {
            return hit + std::min(TILE_SIZE * 0.5F, std::max(0.0F, t - hit - 1.0F));
        } else if (t - hit >= TILE_SIZE * 0.5F) {
            return hit + (TILE_SIZE * 0.5F);
        }
    }
    return hit >= 0.0F ? hit + (TILE_SIZE * 0.5F) : range;
}

static void build_vision(RenderState& render, const WorldGrid* grid, const Tileset* tileset, int w, int h, float sc,
                         glm::vec2 origin, float facing, float range, float half_angle, bool cone) {
    const SDL_FColor lit{1.0F, 1.0F, 1.0F, 1.0F};
    auto project = [&](glm::vec2 wp) -> SDL_FPoint {
        glm::vec2 s = render.camera.worldToScreen(wp, w, h) * sc;
        return {s.x, s.y};
    };
    const float a0 = cone ? (facing - half_angle) : 0.0F;
    const float span = cone ? (2.0F * half_angle) : 6.2831853F;
    constexpr float TAU = 6.2831853F;
    constexpr float EPS = 0.0008F;

    static std::vector<float> rel;
    rel.clear();
    constexpr int BASE = 96;
    for (int i = 0; i <= BASE; ++i) {
        rel.push_back(span * static_cast<float>(i) / static_cast<float>(BASE));
    }
    if (grid != nullptr && tileset != nullptr) {
        auto solid_cell = [&](int cx, int cy) -> bool {
            return ballistics::solid(ballistics::tile_at(*grid, *tileset, (static_cast<float>(cx) + 0.5F) * TILE_SIZE, (static_cast<float>(cy) + 0.5F) * TILE_SIZE));
        };
        int x0 = static_cast<int>(std::floor((origin.x - range) / TILE_SIZE));
        int x1 = static_cast<int>(std::floor((origin.x + range) / TILE_SIZE));
        int y0 = static_cast<int>(std::floor((origin.y - range) / TILE_SIZE));
        int y1 = static_cast<int>(std::floor((origin.y + range) / TILE_SIZE));
        float r2 = (range + TILE_SIZE) * (range + TILE_SIZE);
        for (int ty = y0; ty <= y1; ++ty) {
            for (int tx = x0; tx <= x1; ++tx) {
                if (!solid_cell(tx, ty)) {
                    continue;
                }
                bool l = solid_cell(tx - 1, ty);
                bool r = solid_cell(tx + 1, ty);
                bool u = solid_cell(tx, ty - 1);
                bool d = solid_cell(tx, ty + 1);
                const std::array<std::pair<bool, glm::vec2>, 4> corners{{
                    {!l && !u, {static_cast<float>(tx) * TILE_SIZE, static_cast<float>(ty) * TILE_SIZE}},
                    {!r && !u, {(static_cast<float>(tx) + 1.0F) * TILE_SIZE, static_cast<float>(ty) * TILE_SIZE}},
                    {!l && !d, {static_cast<float>(tx) * TILE_SIZE, (static_cast<float>(ty) + 1.0F) * TILE_SIZE}},
                    {!r && !d, {(static_cast<float>(tx) + 1.0F) * TILE_SIZE, (static_cast<float>(ty) + 1.0F) * TILE_SIZE}},
                }};
                for (const auto& [exposed, c] : corners) {
                    if (!exposed) {
                        continue;
                    }
                    glm::vec2 dvec = c - origin;
                    float d2 = glm::dot(dvec, dvec);
                    if (d2 > r2) {
                        continue;
                    }
                    float ang = std::atan2(dvec.y, dvec.x);
                    float rl = std::fmod(ang - a0, TAU);
                    if (rl < 0.0F) {
                        rl += TAU;
                    }
                    if (cone && rl > span) {
                        continue;
                    }
                    float eps = std::clamp(1.5F / std::sqrt(std::max(d2, 1.0F)), EPS, 0.08F);
                    rel.push_back(std::clamp(rl - eps, 0.0F, span));
                    rel.push_back(rl);
                    rel.push_back(std::clamp(rl + eps, 0.0F, span));
                }
            }
        }
    }
    std::sort(rel.begin(), rel.end());

    g_vis_verts.clear();
    g_vis_idx.clear();
    g_vis_verts.push_back({.position = project(origin), .color = lit, .tex_coord = {0, 0}});
    for (float rl : rel) {
        float ang = a0 + rl;
        glm::vec2 dir{std::cos(ang), std::sin(ang)};
        float dist = range;
        if (grid != nullptr && tileset != nullptr) {
            dist = std::min(range, cast_ray(*grid, *tileset, origin, dir, range));
        }
        g_vis_verts.push_back({.position = project(origin + (dir * dist)), .color = lit, .tex_coord = {0, 0}});
    }
    int n = static_cast<int>(g_vis_verts.size()) - 1;
    for (int i = 1; i < n; ++i) {
        g_vis_idx.push_back(0);
        g_vis_idx.push_back(i);
        g_vis_idx.push_back(i + 1);
    }
}

static void blit_vision(RenderState& render) {
    if (g_vis_idx.empty()) {
        return;
    }
    SDL_SetRenderDrawBlendMode(render.target, add_rgba());
    SDL_RenderGeometry(render.target, nullptr, g_vis_verts.data(), static_cast<int>(g_vis_verts.size()), g_vis_idx.data(), static_cast<int>(g_vis_idx.size()));
}

void Render::postprocess(flecs::iter& it, size_t /*i*/, RenderState& render) {
    flecs::world world = it.world();
    static flecs::query<const Camera> cam_q = world.query_builder<const Camera>().with<Local>().build();
    static flecs::query<const Position, const Light> light_q = world.query_builder<const Position, const Light>().build();

    Camera cam{};
    bool have = false;
    glm::vec2 origin{0};
    float facing = 0.0F;
    cam_q.each([&](flecs::entity e, const Camera& c) -> void {
        cam = c;
        have = true;
        if (const auto* p = e.try_get<Position>()) { origin = p->value; }
        if (const auto* r = e.try_get<Rotation>()) { facing = r->angle; }
    });
    if (have && cam.target != 0) {
        flecs::entity t = world.entity(cam.target);
        if (t.is_alive()) {
            if (const auto* p = t.try_get<Position>()) { origin = p->value; }
            if (const auto* r = t.try_get<Rotation>()) { facing = r->angle; }
        }
    }

    int w = 0;
    int h = 0;
    SDL_GetWindowSize(render.window, &w, &h);
    int pw = 0;
    int ph = 0;
    SDL_GetCurrentRenderOutputSize(render.target, &pw, &ph);
    float sc = (w > 0) ? static_cast<float>(pw) / static_cast<float>(w) : 1.0F;
    float prevX = 1.0F;
    float prevY = 1.0F;
    SDL_GetRenderScale(render.target, &prevX, &prevY);
    SDL_FRect pfull = {.x = 0, .y = 0, .w = static_cast<float>(pw), .h = static_cast<float>(ph)};
    SDL_Texture* frame = SDL_GetRenderTarget(render.target);

    SDL_SetRenderScale(render.target, 1.0F, 1.0F);

    auto compose_entities = [&]() -> void {
        if (render.entityTexture != nullptr) {
            SDL_SetTextureBlendMode(render.entityTexture, premult_blend());
            SDL_RenderTexture(render.target, render.entityTexture, nullptr, &pfull);
        }
    };

    const bool dark = have && (cam.ambient < 0.999F || cam.vision != VisionKind::None);
    if (dark) {
        auto ensure = [&](SDL_Texture*& t, int tw, int th) -> void {
            if (t != nullptr) {
                float cw = 0;
                float ch = 0;
                SDL_GetTextureSize(t, &cw, &ch);
                if (static_cast<int>(cw) == tw && static_cast<int>(ch) == th) { return; }
                SDL_DestroyTexture(t);
            }
            t = SDL_CreateTexture(render.target, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        };
        ensure(render.lightTexture, pw, ph);
        ensure(render.maskTexture, pw, ph);
        ensure(render.lightBlur, std::max(1, pw / 2), std::max(1, ph / 2));
        ensure(render.lightBlur2, std::max(1, pw / 2), std::max(1, ph / 2));

        if (cam.vision != VisionKind::None) {
            build_vision(render, world.try_get<WorldGrid>(), world.try_get<Tileset>(), w, h, sc, origin, facing, cam.vision_range, cam.vision_angle, cam.vision == VisionKind::Cone);
        }

        Uint8 wf = static_cast<Uint8>(std::clamp(cam.ambient, 0.0F, 1.0F) * 150.0F);
        SDL_SetRenderTarget(render.target, render.lightTexture);
        SDL_SetRenderDrawColor(render.target, wf, wf, wf, wf);
        SDL_RenderClear(render.target);
        if (cam.vision != VisionKind::None) { blit_vision(render); }
        SDL_SetRenderTarget(render.target, render.maskTexture);
        SDL_SetRenderDrawColor(render.target, 0, 0, 0, 0);
        SDL_RenderClear(render.target);
        if (cam.vision != VisionKind::None) { blit_vision(render); }
        light_q.each([&](flecs::entity /*e*/, const Position& p, const Light& l) -> void {
            glm::vec2 s = render.camera.worldToScreen(p.value, w, h) * sc;
            float rad = l.radius * render.camera.zoom * sc;
            Uint8 a = static_cast<Uint8>(std::clamp(l.intensity, 0.0F, 1.0F) * 255.0F);
            for (SDL_Texture* m : {render.lightTexture, render.maskTexture}) {
                SDL_SetRenderTarget(render.target, m);
                if (render.smokeTexture != nullptr) {
                    SDL_SetTextureBlendMode(render.smokeTexture, add_rgba());
                    SDL_SetTextureColorMod(render.smokeTexture, static_cast<Uint8>(l.r), static_cast<Uint8>(l.g), static_cast<Uint8>(l.b));
                    SDL_SetTextureAlphaMod(render.smokeTexture, a);
                    SDL_FRect dst = {.x = s.x - rad, .y = s.y - rad, .w = rad * 2.0F, .h = rad * 2.0F};
                    SDL_RenderTexture(render.target, render.smokeTexture, nullptr, &dst);
                    SDL_SetTextureColorMod(render.smokeTexture, 255, 255, 255);
                    SDL_SetTextureAlphaMod(render.smokeTexture, 255);
                }
            }
        });

        auto copy = [&](SDL_Texture* src, SDL_Texture* dst, float dw, float dh) -> void {
            SDL_SetRenderTarget(render.target, dst);
            SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
            SDL_FRect r = {.x = 0, .y = 0, .w = dw, .h = dh};
            SDL_RenderTexture(render.target, src, nullptr, &r);
        };
        auto gauss = [&](SDL_Texture* src, SDL_Texture* dst, float dx, float dy) -> void {
            static const std::array<float, 9> wt{0.0039F, 0.0313F, 0.1094F, 0.2188F, 0.2734F, 0.2188F, 0.1094F, 0.0313F, 0.0039F};
            static const std::array<float, 9> off{-4, -3, -2, -1, 0, 1, 2, 3, 4};
            float tw = 0;
            float th = 0;
            SDL_GetTextureSize(src, &tw, &th);
            SDL_SetRenderTarget(render.target, dst);
            SDL_SetRenderDrawColor(render.target, 0, 0, 0, 0);
            SDL_RenderClear(render.target);
            SDL_SetTextureBlendMode(src, accum_blend());
            SDL_FRect dstr = {.x = 0, .y = 0, .w = tw, .h = th};
            for (size_t k = 0; k < wt.size(); ++k) {
                auto wb = static_cast<Uint8>(wt[k] * 255.0F);
                SDL_SetTextureColorMod(src, wb, wb, wb);
                SDL_SetTextureAlphaMod(src, wb);
                SDL_FRect srcr = {.x = off[k] * dx, .y = off[k] * dy, .w = tw, .h = th};
                SDL_RenderTexture(render.target, src, &srcr, &dstr);
            }
            SDL_SetTextureColorMod(src, 255, 255, 255);
            SDL_SetTextureAlphaMod(src, 255);
        };
        constexpr float SPREAD = 1.6F;
        float hw = pfull.w / 2.0F;
        float hh = pfull.h / 2.0F;
        auto blur = [&](SDL_Texture* tex) -> void {
            copy(tex, render.lightBlur, hw, hh);
            gauss(render.lightBlur, render.lightBlur2, SPREAD, 0.0F);
            gauss(render.lightBlur2, render.lightBlur, 0.0F, SPREAD);
            copy(render.lightBlur, tex, pfull.w, pfull.h);
        };
        blur(render.lightTexture);
        blur(render.maskTexture);

        SDL_SetRenderTarget(render.target, frame);
        SDL_SetTextureBlendMode(render.lightTexture, multiply_blend());
        SDL_RenderTexture(render.target, render.lightTexture, nullptr, &pfull);
        SDL_SetRenderTarget(render.target, render.entityTexture);
        SDL_SetRenderScale(render.target, 1.0F, 1.0F);
        SDL_SetTextureBlendMode(render.maskTexture, mask_mul());
        SDL_RenderTexture(render.target, render.maskTexture, nullptr, &pfull);
        SDL_SetRenderTarget(render.target, frame);
    }

    compose_entities();

    if (have && cam.tint_a > 0.001F) {
        fullscreen(render.target, pw, ph, static_cast<Uint8>(cam.tint_r), static_cast<Uint8>(cam.tint_g), static_cast<Uint8>(cam.tint_b), static_cast<Uint8>(std::clamp(cam.tint_a, 0.0F, 1.0F) * 255.0F));
    }
    if (have && cam.flash > 0.001F) {
        fullscreen(render.target, pw, ph, 255, 255, 255, static_cast<Uint8>(std::clamp(cam.flash, 0.0F, 1.0F) * 255.0F));
    }

    SDL_SetRenderScale(render.target, prevX, prevY);
}

static auto vignette_tex(RenderState& render, int w, int h) -> SDL_Texture* {
    if (render.vignetteTexture != nullptr) {
        return render.vignetteTexture;
    }
    render.vignetteTexture = SDL_CreateTexture(render.target, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureBlendMode(render.vignetteTexture, SDL_BLENDMODE_BLEND);
    SDL_Texture* prev = SDL_GetRenderTarget(render.target);
    SDL_SetRenderTarget(render.target, render.vignetteTexture);
    SDL_SetRenderDrawColor(render.target, 0, 0, 0, 255);
    SDL_RenderClear(render.target);
    if (render.smokeTexture != nullptr) {
        SDL_BlendMode cut = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                                       SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
        SDL_SetTextureBlendMode(render.smokeTexture, cut);
        float sz = static_cast<float>(std::max(w, h)) * 1.55F;
        SDL_FRect dst = {.x = (static_cast<float>(w) - sz) / 2.0F, .y = (static_cast<float>(h) - sz) / 2.0F, .w = sz, .h = sz};
        SDL_RenderTexture(render.target, render.smokeTexture, nullptr, &dst);
        SDL_SetTextureBlendMode(render.smokeTexture, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderTarget(render.target, prev);
    return render.vignetteTexture;
}

auto Render::effects(RenderState& render, SDL_Texture* cur, const Camera& cam, int w, int h) -> SDL_Texture* {
    bool blur = cam.blur > 0.1F;
    bool chroma = cam.chromatic > 0.1F;
    bool vign = cam.vignette > 0.01F;
    if (!blur && !chroma && !vign) {
        return cur;
    }
    if (render.effectsTexture == nullptr) {
        render.effectsTexture = SDL_CreateTexture(render.target, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    }
    SDL_FRect full = {.x = 0, .y = 0, .w = static_cast<float>(w), .h = static_cast<float>(h)};

    SDL_SetRenderTarget(render.target, render.effectsTexture);
    SDL_SetRenderDrawColor(render.target, 0, 0, 0, 255);
    SDL_RenderClear(render.target);
    if (chroma) {
        float o = std::clamp(cam.chromatic, 0.0F, 14.0F);
        SDL_SetTextureBlendMode(cur, SDL_BLENDMODE_ADD);
        SDL_FRect rr = {-o, 0, full.w, full.h};
        SDL_SetTextureColorMod(cur, 255, 0, 0);
        SDL_RenderTexture(render.target, cur, nullptr, &rr);
        SDL_SetTextureColorMod(cur, 0, 255, 0);
        SDL_RenderTexture(render.target, cur, nullptr, &full);
        SDL_FRect br = {o, 0, full.w, full.h};
        SDL_SetTextureColorMod(cur, 0, 0, 255);
        SDL_RenderTexture(render.target, cur, nullptr, &br);
        SDL_SetTextureColorMod(cur, 255, 255, 255);
        SDL_SetTextureBlendMode(cur, SDL_BLENDMODE_BLEND);
    } else {
        SDL_SetTextureBlendMode(cur, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(render.target, cur, nullptr, &full);
    }
    SDL_Texture* out = render.effectsTexture;

    if (blur) {
        int bw = std::max(1, w / 3);
        int bh = std::max(1, h / 3);
        if (render.effectsHalf == nullptr) {
            render.effectsHalf = SDL_CreateTexture(render.target, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, bw, bh);
        }
        SDL_SetTextureScaleMode(out, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureScaleMode(render.effectsHalf, SDL_SCALEMODE_LINEAR);
        int passes = std::clamp(static_cast<int>(cam.blur), 1, 5);
        for (int i = 0; i < passes; ++i) {
            SDL_SetRenderTarget(render.target, render.effectsHalf);
            SDL_SetTextureBlendMode(out, SDL_BLENDMODE_NONE);
            SDL_RenderTexture(render.target, out, nullptr, nullptr);
            SDL_SetRenderTarget(render.target, out);
            SDL_SetTextureBlendMode(render.effectsHalf, SDL_BLENDMODE_NONE);
            SDL_RenderTexture(render.target, render.effectsHalf, nullptr, &full);
        }
    }

    if (vign) {
        SDL_Texture* v = vignette_tex(render, w, h);
        SDL_SetRenderTarget(render.target, out);
        SDL_SetTextureAlphaMod(v, static_cast<Uint8>(std::clamp(cam.vignette, 0.0F, 1.0F) * 255.0F));
        SDL_RenderTexture(render.target, v, nullptr, &full);
        SDL_SetTextureAlphaMod(v, 255);
    }
    return out;
}

void Render::entities_begin(flecs::iter& /*it*/, size_t /*i*/, RenderState& render) {
    int pw = 0;
    int ph = 0;
    SDL_GetCurrentRenderOutputSize(render.target, &pw, &ph);
    float cw = 0;
    float ch = 0;
    if (render.entityTexture != nullptr) {
        SDL_GetTextureSize(render.entityTexture, &cw, &ch);
    }
    if (render.entityTexture == nullptr || static_cast<int>(cw) != pw || static_cast<int>(ch) != ph) {
        if (render.entityTexture != nullptr) {
            SDL_DestroyTexture(render.entityTexture);
        }
        render.entityTexture = SDL_CreateTexture(render.target, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, pw, ph);
        SDL_SetTextureBlendMode(render.entityTexture, SDL_BLENDMODE_BLEND);
    }
    SDL_SetRenderTarget(render.target, render.entityTexture);
    int lw = 0;
    int lh = 0;
    SDL_GetWindowSize(render.window, &lw, &lh);
    float sc = (lw > 0) ? static_cast<float>(pw) / static_cast<float>(lw) : 1.0F;
    SDL_SetRenderScale(render.target, sc, sc);
    SDL_SetRenderDrawColor(render.target, 0, 0, 0, 0);
    SDL_RenderClear(render.target);
}

void Render::entities_end(flecs::iter& /*it*/, size_t /*i*/, RenderState& render) {
    SDL_SetRenderTarget(render.target, render.curIsA ? render.frameA : render.frameB);
}
