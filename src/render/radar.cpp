#include <array>
#include <cmath>

#include "component/network.h"
#include "component/script.h"
#include "component/world.h"
#include "render.h"
#include "util/ballistics.h"

namespace {
constexpr int CHUNK = 32;
constexpr float TILE = 32.0F;
constexpr float NEAR = 360.0F;
constexpr float CORNER = 9.0F;
constexpr float MARGIN = 16.0F;

void fill_circle(SDL_Renderer* r, float cx, float cy, float rad, SDL_Color col) {
    constexpr int SEG = 16;
    std::array<SDL_Vertex, SEG + 2> verts{};
    std::array<int, SEG * 3> idx{};
    SDL_FColor fc{static_cast<float>(col.r) / 255.0F, static_cast<float>(col.g) / 255.0F, static_cast<float>(col.b) / 255.0F, static_cast<float>(col.a) / 255.0F};
    verts[0] = {.position = {cx, cy}, .color = fc, .tex_coord = {0, 0}};
    for (int i = 0; i <= SEG; ++i) {
        float a = (static_cast<float>(i) / SEG) * 6.2831853F;
        verts[i + 1] = {.position = {cx + (std::cos(a) * rad), cy + (std::sin(a) * rad)}, .color = fc, .tex_coord = {0, 0}};
    }
    for (int i = 0; i < SEG; ++i) {
        idx[(i * 3) + 0] = 0;
        idx[(i * 3) + 1] = i + 1;
        idx[(i * 3) + 2] = i + 2;
    }
    SDL_RenderGeometry(r, nullptr, verts.data(), SEG + 2, idx.data(), SEG * 3);
}

void blit_round_panel(SDL_Renderer* r, float x, float y, float w, float h, float rad, SDL_Texture* tex, Uint8 alpha) {
    constexpr int ARC = 4;
    constexpr int RIM = 4 * (ARC + 1);
    std::array<SDL_Vertex, RIM + 1> verts{};
    std::array<int, RIM * 3> idx{};
    SDL_FColor fc{1.0F, 1.0F, 1.0F, static_cast<float>(alpha) / 255.0F};
    auto vert = [&](int n, float px, float py) { verts[n] = {.position = {px, py}, .color = fc, .tex_coord = {(px - x) / w, (py - y) / h}}; };
    vert(0, x + (w * 0.5F), y + (h * 0.5F));
    int n = 1;
    const std::array<std::array<float, 3>, 4> corners{{
        {{x + w - rad, y + rad, -1.5707963F}},
        {{x + w - rad, y + h - rad, 0.0F}},
        {{x + rad, y + h - rad, 1.5707963F}},
        {{x + rad, y + rad, 3.1415927F}},
    }};
    for (const auto& c : corners) {
        for (int i = 0; i <= ARC; ++i) {
            float a = c[2] + (static_cast<float>(i) / ARC * 1.5707963F);
            vert(n++, c[0] + (std::cos(a) * rad), c[1] + (std::sin(a) * rad));
        }
    }
    for (int i = 0; i < RIM; ++i) {
        idx[(i * 3) + 0] = 0;
        idx[(i * 3) + 1] = i + 1;
        idx[(i * 3) + 2] = ((i + 1) % RIM) + 1;
    }
    SDL_RenderGeometry(r, tex, verts.data(), RIM + 1, idx.data(), RIM * 3);
}
}

static auto los_clear(const WorldGrid* grid, const Tileset* tileset, glm::vec2 a, glm::vec2 b) -> bool {
    if (grid == nullptr || tileset == nullptr) {
        return true;
    }
    glm::vec2 d = b - a;
    int steps = static_cast<int>(glm::length(d) / 20.0F);
    for (int i = 1; i < steps; ++i) {
        glm::vec2 p = a + (d * (static_cast<float>(i) / static_cast<float>(steps)));
        if (ballistics::solid(ballistics::tile_at(*grid, *tileset, p.x, p.y))) {
            return false;
        }
    }
    return true;
}

static auto find_minimap(const ViewWidget& w) -> const ViewWidget* {
    if (w.kind == ViewKind::Minimap) {
        return &w;
    }
    for (const auto& c : w.children) {
        if (const ViewWidget* found = find_minimap(c)) {
            return found;
        }
    }
    return nullptr;
}

static auto box_origin(ViewPlacement placement, float box, int w, int h) -> glm::vec2 {
    float fw = static_cast<float>(w);
    float fh = static_cast<float>(h);
    switch (placement) {
        case ViewPlacement::TopRight:
            return {fw - box - MARGIN, MARGIN};
        case ViewPlacement::BottomRight:
            return {fw - box - MARGIN, fh - box - MARGIN};
        case ViewPlacement::Bottom:
            return {(fw - box) * 0.5F, fh - box - MARGIN};
        case ViewPlacement::Center:
            return {(fw - box) * 0.5F, (fh - box) * 0.5F};
        case ViewPlacement::BottomLeft:
        default:
            return {MARGIN, fh - box - MARGIN};
    }
}

void Render::radar(flecs::iter& it, size_t /*i*/, RenderState& render) {
    flecs::world world = it.world();
    const auto* views = world.try_get<ViewState>();
    if (views == nullptr) {
        return;
    }
    const ViewWidget* node = nullptr;
    ViewPlacement placement = ViewPlacement::BottomLeft;
    for (const auto& v : views->views) {
        if (const ViewWidget* found = find_minimap(v.root)) {
            node = found;
            placement = v.placement;
            break;
        }
    }
    if (node == nullptr) {
        return;
    }
    const float box = node->number > 0.0F ? node->number : 200.0F;
    const float range = node->number_max > 0.0F ? node->number_max : 1700.0F;

    static flecs::query<const Position> self_q = world.query_builder<const Position>().with<Local>().with<Tank>().build();
    static flecs::query<const Position, const Color> tank_q = world.query_builder<const Position, const Color>().with<Tank>().without<Dying>().build();

    flecs::entity me;
    glm::vec2 center = render.camera.position;
    self_q.each([&](flecs::entity e, const Position& p) -> void { me = e; center = p.value; });

    int boxpx = std::max(1, static_cast<int>(box));
    float cw = 0;
    float ch = 0;
    if (render.radarTexture != nullptr) {
        SDL_GetTextureSize(render.radarTexture, &cw, &ch);
    }
    if (render.radarTexture == nullptr || static_cast<int>(cw) != boxpx) {
        if (render.radarTexture != nullptr) {
            SDL_DestroyTexture(render.radarTexture);
        }
        render.radarTexture = SDL_CreateTexture(render.target, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, boxpx, boxpx);
        SDL_SetTextureBlendMode(render.radarTexture, SDL_BLENDMODE_BLEND);
    }
    auto local = [&](glm::vec2 wp) -> glm::vec2 { return {(box * 0.5F) + ((wp.x - center.x) / range * box), (box * 0.5F) + ((wp.y - center.y) / range * box)}; };

    SDL_Texture* prev = SDL_GetRenderTarget(render.target);
    SDL_SetRenderTarget(render.target, render.radarTexture);
    SDL_SetRenderDrawBlendMode(render.target, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(render.target, 14, 17, 24, 255);
    SDL_RenderClear(render.target);

    const auto* grid = world.try_get<WorldGrid>();
    const auto* tileset = world.try_get<Tileset>();
    if (grid != nullptr && tileset != nullptr) {
        float ts = std::ceil(TILE / range * box) + 1.0F;
        SDL_SetRenderDrawColor(render.target, 70, 82, 100, 255);
        for (const auto& [key, chunk] : grid->data) {
            float ccx = ((static_cast<float>(chunk.cx) * CHUNK) + (CHUNK / 2.0F)) * TILE;
            float ccy = ((static_cast<float>(chunk.cy) * CHUNK) + (CHUNK / 2.0F)) * TILE;
            if (std::abs(ccx - center.x) > range + (CHUNK * TILE) || std::abs(ccy - center.y) > range + (CHUNK * TILE)) {
                continue;
            }
            for (int t = 0; t < CHUNK * CHUNK; ++t) {
                uint16_t id = chunk.tiles[t];
                if (id == 0 || !tileset->type(id).solid) {
                    continue;
                }
                glm::vec2 rp = local({((static_cast<float>(chunk.cx) * CHUNK) + (t % CHUNK) + 0.5F) * TILE, ((static_cast<float>(chunk.cy) * CHUNK) + (static_cast<float>(t) / CHUNK) + 0.5F) * TILE});
                SDL_FRect cell = {.x = rp.x - (ts / 2.0F), .y = rp.y - (ts / 2.0F), .w = ts, .h = ts};
                SDL_RenderFillRect(render.target, &cell);
            }
        }
    }

    for (const Blip& b : node->blips) {
        glm::vec2 rp = local({b.x, b.y});
        if (b.radius > 0.0F) {
            fill_circle(render.target, rp.x, rp.y, (b.radius / range) * box, SDL_Color{b.r, b.g, b.b, b.a});
        }
    }
    for (const Blip& b : node->blips) {
        if (b.radius > 0.0F) {
            continue;
        }
        glm::vec2 rp = local({b.x, b.y});
        fill_circle(render.target, rp.x, rp.y, 4.0F, SDL_Color{b.r, b.g, b.b, b.a});
    }

    tank_q.each([&](flecs::entity e, const Position& p, const Color& c) -> void {
        bool self = (e == me);
        if (!self && glm::distance(p.value, center) > NEAR && !los_clear(grid, tileset, center, p.value)) {
            return;
        }
        glm::vec2 rp = local(p.value);
        SDL_Color col = self ? SDL_Color{255, 255, 255, 255}
                             : SDL_Color{static_cast<Uint8>(c.value.x), static_cast<Uint8>(c.value.y), static_cast<Uint8>(c.value.z), 255};
        fill_circle(render.target, rp.x, rp.y, self ? 5.0F : 4.0F, col);
    });

    SDL_SetRenderTarget(render.target, prev);
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(render.window, &w, &h);
    glm::vec2 o = box_origin(placement, box, w, h);
    blit_round_panel(render.target, o.x, o.y, box, box, CORNER, render.radarTexture, 235);
}
