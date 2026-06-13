#include "widget.h"

#include <algorithm>
#include <string>
#include <vector>

#include "util/format.h"


namespace {

struct Segment {
    std::string text;
    format::Text style;
};

auto seg_color(const format::Text& s, Clay_Color base) -> Clay_Color {
    if (!s.hasColor) {
        return base;
    }
    return {.r = static_cast<float>(s.color.r), .g = static_cast<float>(s.color.g), .b = static_cast<float>(s.color.b), .a = base.a};
}

void emit_segment(InterfaceState& state, const Segment& seg, uint16_t fontSize, Clay_Color base) {
    const std::string& s = state.intern(seg.text);
    if (s.empty()) {
        return;
    }
    const Clay_Color color = seg_color(seg.style, base);
    const uint16_t fontId = seg.style.italic ? format::FONT_ITALIC : format::FONT_NORMAL;
    auto cfg = CLAY_TEXT_CONFIG({.textColor = color, .fontId = fontId, .fontSize = fontSize, .wrapMode = CLAY_TEXT_WRAP_NONE});

    const bool decorated = seg.style.bold || seg.style.underline || seg.style.strike;
    if (!decorated) {
        CLAY_TEXT(Str(s), cfg);
        return;
    }

    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(), CLAY_SIZING_FIT()}}}) {
        CLAY_TEXT(Str(s), cfg);

        if (seg.style.bold) {
            float dx = std::max(0.6F, static_cast<float>(fontSize) * 0.03F);
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()}},
                  .floating = {.offset = {dx, 0.0F},
                               .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                               .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                               .attachTo = CLAY_ATTACH_TO_PARENT}}) {
                CLAY_TEXT(Str(s), cfg);
            }
        }

        if (seg.style.underline || seg.style.strike) {
            const format::Font* metricFont = (seg.style.italic && state.fontItalic != nullptr) ? state.fontItalic : state.font;
            float w = static_cast<float>(format::width(metricFont, s.data(), s.size(), fontSize));
            float lh = static_cast<float>(format::line_height(state.font, fontSize));
            float thickness = std::max(1.0F, static_cast<float>(fontSize) / 16.0F);
            auto rule = [&](float y) -> void {
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(thickness)}},
                      .backgroundColor = color,
                      .floating = {.offset = {0.0F, y},
                                   .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                                   .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                                   .attachTo = CLAY_ATTACH_TO_PARENT}}) {}
            };
            if (seg.style.underline) {
                rule(lh * 0.92F);
            }
            if (seg.style.strike) {
                rule(lh * 0.52F);
            }
        }
    }
}

}

void widget::rich(InterfaceState& state, const std::string& text, uint16_t fontSize, Clay_Color base, Clay_LayoutAlignmentX align, bool edit) {
    const format::Mode mode = edit ? format::Mode::Edit : format::Mode::Display;
    const format::Text dim{.color = {130, 130, 140, 255}, .hasColor = true};

    std::vector<std::vector<Segment>> rows(1);
    format::Text style;
    size_t start = 0;
    size_t n = text.size();
    for (size_t i = 0; i <= n; ++i) {
        if (i != n && text[i] != '\n') {
            continue;
        }
        style = format::scan(text.data() + start, i - start, mode,
                             [&](const format::Span& s) -> void {
                                 rows.back().push_back({.text = std::string(s.data, s.length), .style = s.code ? dim : s.style});
                             },
                             style);
        if (i != n) {
            rows.emplace_back();
        }
        start = i + 1;
    }

    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(), CLAY_SIZING_FIT()}, .childAlignment = {.x = align}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        for (const auto& row : rows) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(), CLAY_SIZING_FIT()}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}, .layoutDirection = CLAY_LEFT_TO_RIGHT}}) {
                for (const Segment& seg : row) {
                    emit_segment(state, seg, fontSize, base);
                }
            }
        }
    }
}
