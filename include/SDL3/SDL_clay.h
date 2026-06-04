#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <clay.h>

#include <numbers>

#include "util/format.h"

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
                const char* chars = d.stringContents.chars;
                const auto length = static_cast<size_t>(d.stringContents.length);
                const auto alpha = static_cast<Uint8>(d.textColor.a);
                const bool editMode = d.fontId == FONT_EDIT;

                TextFormat fmt;
                const char* base = d.stringContents.baseChars;
                for (const char* p = base; p != nullptr && p < chars;) {
                    if (format_is_escape(p, static_cast<size_t>(chars - p))) {
                        p += 4;
                    } else if (format_is_code(p, static_cast<size_t>(chars - p))) {
                        format_apply(p[2], fmt);
                        p += 3;
                    } else {
                        ++p;
                    }
                }

                const bool layered = alpha < 255;
                const Uint8 drawAlpha = layered ? 255 : alpha;

                float scaleX = 1.0F;
                float scaleY = 1.0F;
                int layerW = 0;
                int layerH = 0;
                SDL_Texture* layer = nullptr;
                SDL_Texture* savedTarget = nullptr;
                SDL_Rect savedClip = {};
                bool savedClipEnabled = false;
                float penX = SDL_roundf(rect.x);
                float penY = SDL_roundf(rect.y);

                if (layered) {
                    SDL_GetRenderScale(rd.renderer, &scaleX, &scaleY);
                    layerW = static_cast<int>(SDL_ceilf((rect.w + static_cast<float>(d.fontSize)) * scaleX));
                    layerH = static_cast<int>(SDL_ceilf((rect.h + static_cast<float>(d.fontSize)) * scaleY));
                    layer = SDL_CreateTexture(rd.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, std::max(1, layerW), std::max(1, layerH));
                    SDL_SetTextureBlendMode(layer, SDL_BLENDMODE_BLEND);
                    savedTarget = SDL_GetRenderTarget(rd.renderer);
                    savedClipEnabled = SDL_RenderClipEnabled(rd.renderer);
                    if (savedClipEnabled) {
                        SDL_GetRenderClipRect(rd.renderer, &savedClip);
                    }
                    SDL_SetRenderTarget(rd.renderer, layer);
                    SDL_SetRenderScale(rd.renderer, scaleX, scaleY);
                    SDL_SetRenderClipRect(rd.renderer, nullptr);
                    SDL_SetRenderDrawColor(rd.renderer, 0, 0, 0, 0);
                    SDL_RenderClear(rd.renderer);
                    penX = 0.0F;
                    penY = 0.0F;
                }

                auto draw_run = [&](size_t from, size_t to, const TextFormat& format) -> void {
                    if (to <= from) {
                        return;
                    }
                    TTF_Font* fontToUse = (format.italic && rd.fonts[1] != nullptr) ? rd.fonts[1] : rd.fonts[0];
                    TTF_SetFontSize(fontToUse, d.fontSize);
                    TTF_SetFontStyle(fontToUse, format.bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL);

                    SDL_Color color = format.hasColor ? SDL_Color{format.color.r, format.color.g, format.color.b, drawAlpha}
                                                      : SDL_Color{static_cast<Uint8>(d.textColor.r), static_cast<Uint8>(d.textColor.g), static_cast<Uint8>(d.textColor.b), drawAlpha};

                    auto* text = TTF_CreateText(rd.textEngine, fontToUse, chars + from, to - from);
                    TTF_SetTextColor(text, color.r, color.g, color.b, color.a);
                    const float x0 = SDL_roundf(penX);
                    TTF_DrawRendererText(text, x0, penY);
                    int textW = 0;
                    int textH = 0;
                    TTF_GetTextSize(text, &textW, &textH);
                    penX += static_cast<float>(textW);
                    TTF_DestroyText(text);

                    if (format.underline || format.strike) {
                        int tw = 0;
                        int th = 0;
                        TTF_GetStringSize(fontToUse, chars + from, to - from, &tw, &th);
                        float lineW = static_cast<float>(tw - format_trailing_bearing(fontToUse, chars + from, to - from));
                        if (lineW > 0.0f) {
                            const float thickness = std::max(1.0f, static_cast<float>(d.fontSize) / 14.0f);
                            SDL_SetRenderDrawBlendMode(rd.renderer, SDL_BLENDMODE_BLEND);
                            SDL_SetRenderDrawColor(rd.renderer, color.r, color.g, color.b, color.a);
                            if (format.underline) {
                                SDL_FRect line = {.x = x0, .y = penY + (static_cast<float>(textH) * 0.88f), .w = lineW, .h = thickness};
                                SDL_RenderFillRect(rd.renderer, &line);
                            }
                            if (format.strike) {
                                SDL_FRect line = {.x = x0, .y = penY + (static_cast<float>(textH) * 0.55f), .w = lineW, .h = thickness};
                                SDL_RenderFillRect(rd.renderer, &line);
                            }
                        }
                    }

                    TTF_SetFontStyle(fontToUse, TTF_STYLE_NORMAL);
                };

                TextFormat marker;
                marker.hasColor = true;
                marker.color = {130, 130, 130, 255};

                size_t runStart = 0;
                for (size_t i = 0; i < length;) {
                    if (format_is_escape(chars + i, length - i)) {
                        draw_run(runStart, i, fmt);
                        draw_run(i, editMode ? i + 4 : i + 2, fmt);
                        i += 4;
                        runStart = i;
                    } else if (format_is_code(chars + i, length - i)) {
                        draw_run(runStart, i, fmt);
                        if (editMode) {
                            draw_run(i, i + 3, marker);
                        }
                        format_apply(chars[i + 2], fmt);
                        i += 3;
                        runStart = i;
                    } else if (editMode && length - i >= 2 && static_cast<unsigned char>(chars[i]) == 0xC2 && static_cast<unsigned char>(chars[i + 1]) == 0xA7) {
                        draw_run(runStart, i, fmt);
                        draw_run(i, i + 2, marker);
                        i += 2;
                        runStart = i;
                    } else {
                        ++i;
                    }
                }
                draw_run(runStart, length, fmt);

                if (layered) {
                    SDL_SetRenderTarget(rd.renderer, savedTarget);
                    SDL_SetRenderScale(rd.renderer, scaleX, scaleY);
                    if (savedClipEnabled) {
                        SDL_SetRenderClipRect(rd.renderer, &savedClip);
                    }
                    SDL_SetTextureAlphaMod(layer, alpha);
                    SDL_FRect dst = {.x = SDL_roundf(rect.x), .y = SDL_roundf(rect.y), .w = static_cast<float>(layerW) / scaleX, .h = static_cast<float>(layerH) / scaleY};
                    SDL_RenderTexture(rd.renderer, layer, nullptr, &dst);
                    SDL_DestroyTexture(layer);
                }
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
