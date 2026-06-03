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

void render_arc(const SDL_Clay_RendererData& rd, SDL_FPoint center, float radius, float startDeg, float endDeg, float thickness, const Clay_Color& color) {
    SDL_SetRenderDrawColor(rd.renderer, static_cast<uint8_t>(color.r), static_cast<uint8_t>(color.g), static_cast<uint8_t>(color.b), static_cast<uint8_t>(color.a));

    const float radStart = startDeg * (std::numbers::pi_v<float> / 180.0F);
    const float radEnd = endDeg * (std::numbers::pi_v<float> / 180.0F);
    const int segs = std::clamp(static_cast<int>(radius * 1.5F), CIRCLE_SEGMENTS, MAX_SEGMENTS);
    const float angleStep = (radEnd - radStart) / static_cast<float>(segs);
    constexpr float STEP = 0.4F;

    SDL_FPoint points[MAX_SEGMENTS + 1];

    for (int ring = 1; STEP * static_cast<float>(ring) < thickness - STEP; ++ring) {
        const float t = STEP * static_cast<float>(ring);
        const float cr = std::max(radius - t, 1.0F);
        for (int i = 0; i <= segs; ++i) {
            const float angle = radStart + (static_cast<float>(i) * angleStep);
            points[i] = {
                .x = SDL_roundf(center.x + (SDL_cosf(angle) * cr)),
                .y = SDL_roundf(center.y + (SDL_sinf(angle) * cr)),
            };
        }
        SDL_RenderLines(rd.renderer, points, segs + 1);
    }
}

void render_border(const SDL_Clay_RendererData& rd, const SDL_FRect& rect, Clay_BorderRenderData& cfg) {
    const float minR = std::min(rect.w, rect.h) / 2.0F;
    const Clay_CornerRadius cr = {
        .topLeft = std::min(cfg.cornerRadius.topLeft, minR),
        .topRight = std::min(cfg.cornerRadius.topRight, minR),
        .bottomLeft = std::min(cfg.cornerRadius.bottomLeft, minR),
        .bottomRight = std::min(cfg.cornerRadius.bottomRight, minR),
    };

    SDL_SetRenderDrawColor(rd.renderer, static_cast<uint8_t>(cfg.color.r), static_cast<uint8_t>(cfg.color.g), static_cast<uint8_t>(cfg.color.b), static_cast<uint8_t>(cfg.color.a));

    auto fill = [&](float x, float y, float w, float h) -> void {
        SDL_FRect r = {.x = x, .y = y, .w = w, .h = h};
        SDL_RenderFillRect(rd.renderer, &r);
    };

    if (cfg.width.left > 0) {
        fill(rect.x - 1, rect.y + cr.topLeft, static_cast<float>(cfg.width.left), rect.h - cr.topLeft - cr.bottomLeft);
    }
    if (cfg.width.right > 0) {
        fill(rect.x + rect.w - static_cast<float>(cfg.width.right) + 1, rect.y + cr.topRight, static_cast<float>(cfg.width.right), rect.h - cr.topRight - cr.bottomRight);
    }
    if (cfg.width.top > 0) {
        fill(rect.x + cr.topLeft, rect.y - 1, rect.w - cr.topLeft - cr.topRight, static_cast<float>(cfg.width.top));
    }
    if (cfg.width.bottom > 0) {
        fill(rect.x + cr.bottomLeft, rect.y + rect.h - static_cast<float>(cfg.width.bottom) + 1, rect.w - cr.bottomLeft - cr.bottomRight, static_cast<float>(cfg.width.bottom));
    }

    auto arc = [&](float radius, float cx, float cy, float start, float end, float w) -> void {
        if (radius > 0) {
            render_arc(rd, {.x = cx, .y = cy}, radius, start, end, w, cfg.color);
        }
    };

    arc(cr.topLeft, rect.x + cr.topLeft - 1, rect.y + cr.topLeft - 1, 180, 270, cfg.width.top);
    arc(cr.topRight, rect.x + rect.w - cr.topRight, rect.y + cr.topRight - 1, 270, 360, cfg.width.top);
    arc(cr.bottomLeft, rect.x + cr.bottomLeft - 1, rect.y + rect.h - cr.bottomLeft, 90, 180, cfg.width.bottom);
    arc(cr.bottomRight, rect.x + rect.w - cr.bottomRight, rect.y + rect.h - cr.bottomRight, 0, 90, cfg.width.bottom);
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
                TTF_DrawRendererText(text, rect.x, rect.y);
                TTF_DestroyText(text);
            } break;

            case CLAY_RENDER_COMMAND_TYPE_BORDER:
                render_border(rd, rect, cmd->renderData.border);
                break;

            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                SDL_Rect clip = {
                    .x = static_cast<int>(cmd->boundingBox.x),
                    .y = static_cast<int>(cmd->boundingBox.y),
                    .w = static_cast<int>(cmd->boundingBox.width),
                    .h = static_cast<int>(cmd->boundingBox.height),
                };
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
