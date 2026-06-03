#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <clay.h>

#include <numbers>

struct SDL_Clay_RendererData {
    SDL_Renderer* renderer;
    TTF_TextEngine* textEngine;
    TTF_Font** fonts;
};

void SDL_Clay_Render(const SDL_Clay_RendererData& rd, Clay_RenderCommandArray& commands);

#ifdef CLAY_RENDERER_IMPLEMENTATION
#undef CLAY_RENDERER_IMPLEMENTATION

#include <algorithm>
#include <array>

namespace {

constexpr int CIRCLE_SEGMENTS = 16;
constexpr int MAX_SEGMENTS = 256;
constexpr int MAX_VERTS = 12 + (8 * MAX_SEGMENTS);
constexpr int MAX_INDICES = 30 + (12 * MAX_SEGMENTS);

struct Corner {
    float cx, cy, sx, sy;
};

auto to_color(const Clay_Color& c) -> SDL_FColor {
    return {.r = c.r / 255.0F, .g = c.g / 255.0F, .b = c.b / 255.0F, .a = c.a / 255.0F};
}

auto to_rect(const Clay_BoundingBox& b) -> SDL_FRect {
    return {.x = b.x, .y = b.y, .w = b.width, .h = b.height};
}

void render_rounded_rect(const SDL_Clay_RendererData& rd, const SDL_FRect& rect, float cornerRadius, const Clay_Color& col) {
    const auto color = to_color(col);
    const float r = std::min(cornerRadius, std::min(rect.w, rect.h) / 2.0F);
    const int segs = std::clamp(static_cast<int>(r * 0.5F), CIRCLE_SEGMENTS, MAX_SEGMENTS);

    SDL_Vertex verts[MAX_VERTS];
    int idx[MAX_INDICES];
    int vc = 0;
    int ic = 0;

    auto push = [&](float x, float y, float u = 0.F, float v = 0.F) -> int {
        verts[vc] = {.position = {.x = x, .y = y}, .color = color, .tex_coord = {.x = u, .y = v}};
        return vc++;
    };

    const int tl = push(rect.x + r, rect.y + r, 0, 0);
    const int tr = push(rect.x + rect.w - r, rect.y + r, 1, 0);
    const int br = push(rect.x + rect.w - r, rect.y + rect.h - r, 1, 1);
    const int bl = push(rect.x + r, rect.y + rect.h - r, 0, 1);

    idx[ic++] = tl;
    idx[ic++] = tr;
    idx[ic++] = bl;
    idx[ic++] = tr;
    idx[ic++] = br;
    idx[ic++] = bl;

    const std::array<Corner, 4> corners = {{
        {.cx = rect.x + r, .cy = rect.y + r, .sx = -1, .sy = -1},
        {.cx = rect.x + rect.w - r, .cy = rect.y + r, .sx = 1, .sy = -1},
        {.cx = rect.x + rect.w - r, .cy = rect.y + rect.h - r, .sx = 1, .sy = 1},
        {.cx = rect.x + r, .cy = rect.y + rect.h - r, .sx = -1, .sy = 1},
    }};

    const float step = (std::numbers::pi_v<float> / 2.0F) / static_cast<float>(segs);

    for (int i = 0; i < segs; ++i) {
        const float a1 = static_cast<float>(i) * step;
        const float a2 = static_cast<float>(i + 1) * step;

        for (int j = 0; j < 4; ++j) {
            const auto& [cx, cy, sx, sy] = corners[j];
            const int v1 = push(cx + (SDL_cosf(a1) * r * sx), cy + (SDL_sinf(a1) * r * sy));
            const int v2 = push(cx + (SDL_cosf(a2) * r * sx), cy + (SDL_sinf(a2) * r * sy));
            idx[ic++] = j;
            idx[ic++] = v1;
            idx[ic++] = v2;
        }
    }

    auto edge = [&](int c0, int c1, float x0, float y0, float x1, float y1) -> void {
        const int e0 = push(x0, y0);
        const int e1 = push(x1, y1);
        idx[ic++] = c0;
        idx[ic++] = e0;
        idx[ic++] = e1;
        idx[ic++] = c1;
        idx[ic++] = c0;
        idx[ic++] = e1;
    };

    edge(tl, tr, rect.x + r, rect.y, rect.x + rect.w - r, rect.y);
    edge(tr, br, rect.x + rect.w, rect.y + r, rect.x + rect.w, rect.y + rect.h - r);
    edge(br, bl, rect.x + rect.w - r, rect.y + rect.h, rect.x + r, rect.y + rect.h);
    edge(bl, tl, rect.x, rect.y + rect.h - r, rect.x, rect.y + r);

    SDL_RenderGeometry(rd.renderer, nullptr, verts, vc, idx, ic);
}

void render_rounded_border(const SDL_Clay_RendererData& rd, const SDL_FRect& rect, float radius, float width, const Clay_Color& color) {
    const auto col = to_color(color);
    const float r = std::min(radius, std::min(rect.w, rect.h) / 2.0F);
    const float ir = std::max(r - width, 0.0F);
    const int segs = std::clamp(static_cast<int>(r * 2.0F), 4, 64);

    constexpr float HALF_PI = std::numbers::pi_v<float> / 2.0F;
    const std::array<float, 4> start = {-HALF_PI, 0.0F, HALF_PI, std::numbers::pi_v<float>};
    const std::array<float, 4> cx = {rect.x + rect.w - r, rect.x + rect.w - r, rect.x + r, rect.x + r};
    const std::array<float, 4> cy = {rect.y + r, rect.y + rect.h - r, rect.y + rect.h - r, rect.y + r};

    SDL_Vertex verts[2 * 4 * 65];
    int idx[6 * 4 * 65];
    int vc = 0;
    int ic = 0;
    int count = 0;

    for (int k = 0; k < 4; ++k) {
        for (int i = 0; i <= segs; ++i) {
            const float a = start[k] + (HALF_PI * static_cast<float>(i) / static_cast<float>(segs));
            const float c = SDL_cosf(a);
            const float s = SDL_sinf(a);
            verts[vc++] = {.position = {.x = cx[k] + (c * r), .y = cy[k] + (s * r)}, .color = col, .tex_coord = {0, 0}};
            verts[vc++] = {.position = {.x = cx[k] + (c * ir), .y = cy[k] + (s * ir)}, .color = col, .tex_coord = {0, 0}};
            ++count;
        }
    }

    for (int i = 0; i < count; ++i) {
        const int o = i * 2;
        const int o2 = ((i + 1) % count) * 2;
        idx[ic++] = o;
        idx[ic++] = o + 1;
        idx[ic++] = o2;
        idx[ic++] = o + 1;
        idx[ic++] = o2 + 1;
        idx[ic++] = o2;
    }

    SDL_RenderGeometry(rd.renderer, nullptr, verts, vc, idx, ic);
}

void render_border(const SDL_Clay_RendererData& rd, const SDL_FRect& rect, Clay_BorderRenderData& cfg) {
    const float minR = std::min(rect.w, rect.h) / 2.0F;
    const float r = std::min(cfg.cornerRadius.topLeft, minR);

    if (r > 0 && cfg.width.top > 0) {
        render_rounded_border(rd, rect, r, static_cast<float>(cfg.width.top), cfg.color);
        return;
    }

    SDL_SetRenderDrawColor(rd.renderer, static_cast<uint8_t>(cfg.color.r), static_cast<uint8_t>(cfg.color.g), static_cast<uint8_t>(cfg.color.b), static_cast<uint8_t>(cfg.color.a));

    auto fill = [&](float x, float y, float w, float h) -> void {
        SDL_FRect rr = {.x = x, .y = y, .w = w, .h = h};
        SDL_RenderFillRect(rd.renderer, &rr);
    };

    if (cfg.width.left > 0) {
        fill(rect.x, rect.y, static_cast<float>(cfg.width.left), rect.h);
    }
    if (cfg.width.right > 0) {
        fill(rect.x + rect.w - static_cast<float>(cfg.width.right), rect.y, static_cast<float>(cfg.width.right), rect.h);
    }
    if (cfg.width.top > 0) {
        fill(rect.x, rect.y, rect.w, static_cast<float>(cfg.width.top));
    }
    if (cfg.width.bottom > 0) {
        fill(rect.x, rect.y + rect.h - static_cast<float>(cfg.width.bottom), rect.w, static_cast<float>(cfg.width.bottom));
    }
}

}

void SDL_Clay_Render(const SDL_Clay_RendererData& rd, Clay_RenderCommandArray& commands) {
    for (int32_t i = 0; i < commands.length; ++i) {
        auto* cmd = Clay_RenderCommandArray_Get(&commands, i);
        const auto rect = to_rect(cmd->boundingBox);

        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                auto& d = cmd->renderData.rectangle;
                SDL_SetRenderDrawBlendMode(rd.renderer, SDL_BLENDMODE_BLEND);
                if (d.cornerRadius.topLeft > 0) {
                    render_rounded_rect(rd, rect, d.cornerRadius.topLeft, d.backgroundColor);
                } else {
                    SDL_SetRenderDrawColor(rd.renderer, static_cast<uint8_t>(d.backgroundColor.r), static_cast<uint8_t>(d.backgroundColor.g), static_cast<uint8_t>(d.backgroundColor.b),
                                           static_cast<uint8_t>(d.backgroundColor.a));
                    SDL_RenderFillRect(rd.renderer, &rect);
                }
            } break;

            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                auto& d = cmd->renderData.text;
                TTF_SetFontSize(rd.fonts[d.fontId], d.fontSize);
                auto* text = TTF_CreateText(rd.textEngine, rd.fonts[d.fontId], d.stringContents.chars, d.stringContents.length);
                TTF_SetTextColor(text, static_cast<Uint8>(d.textColor.r), static_cast<Uint8>(d.textColor.g), static_cast<Uint8>(d.textColor.b), static_cast<Uint8>(d.textColor.a));
                TTF_DrawRendererText(text, SDL_roundf(rect.x), SDL_roundf(rect.y));
                TTF_DestroyText(text);
            } break;

            case CLAY_RENDER_COMMAND_TYPE_BORDER:
                render_border(rd, rect, cmd->renderData.border);
                break;

            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                const int x0 = static_cast<int>(SDL_ceilf(cmd->boundingBox.x));
                const int y0 = static_cast<int>(SDL_ceilf(cmd->boundingBox.y));
                const int x1 = static_cast<int>(SDL_floorf(cmd->boundingBox.x + cmd->boundingBox.width));
                const int y1 = static_cast<int>(SDL_floorf(cmd->boundingBox.y + cmd->boundingBox.height));
                SDL_Rect clip = {.x = x0, .y = y0, .w = std::max(0, x1 - x0), .h = std::max(0, y1 - y0)};
                SDL_SetRenderClipRect(rd.renderer, &clip);
            } break;

            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                SDL_SetRenderClipRect(rd.renderer, nullptr);
                break;

            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                auto* texture = static_cast<SDL_Texture*>(cmd->renderData.image.imageData);
                SDL_RenderTexture(rd.renderer, texture, nullptr, &rect);
            } break;

            default:
                SDL_Log("Unknown render command type: %d", cmd->commandType);
                break;
        }
    }
}

#endif
