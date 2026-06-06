#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace format {

inline constexpr uint16_t FONT_EDIT = 1;

inline auto line_height(TTF_Font* font, uint16_t size) -> int {
    if (font == nullptr) {
        return size;
    }
    TTF_SetFontSize(font, size);
    int w = 0;
    int h = 0;
    TTF_GetStringSize(font, "Ay", 2, &w, &h);
    return h > 0 ? h : size;
}

struct Text {
    SDL_Color color = {0, 0, 0, 255};
    bool hasColor = false;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
};

inline auto color(char code, SDL_Color* out) -> bool {
    struct Entry {
        char code;
        uint32_t rgb;
    };
    static const Entry table[] = {
        {.code = '0', .rgb = 0x000000}, {.code = '1', .rgb = 0x0000AA}, {.code = '2', .rgb = 0x00AA00}, {.code = '3', .rgb = 0x00AAAA},
        {.code = '4', .rgb = 0xAA0000}, {.code = '5', .rgb = 0xAA00AA}, {.code = '6', .rgb = 0xFFAA00}, {.code = '7', .rgb = 0xAAAAAA},
        {.code = '8', .rgb = 0x555555}, {.code = '9', .rgb = 0x5555FF}, {.code = 'a', .rgb = 0x55FF55}, {.code = 'b', .rgb = 0x55FFFF},
        {.code = 'c', .rgb = 0xFF5555}, {.code = 'd', .rgb = 0xFF55FF}, {.code = 'e', .rgb = 0xFFFF55}, {.code = 'f', .rgb = 0xFFFFFF},
    };
    char lower = (code >= 'A' && code <= 'Z') ? static_cast<char>(code + 32) : code;
    for (const auto& entry : table) {
        if (entry.code == lower) {
            out->r = static_cast<Uint8>((entry.rgb >> 16) & 0xFF);
            out->g = static_cast<Uint8>((entry.rgb >> 8) & 0xFF);
            out->b = static_cast<Uint8>(entry.rgb & 0xFF);
            out->a = 255;
            return true;
        }
    }
    return false;
}

inline auto apply(char code, Text& format) -> bool {
    SDL_Color parsed = {};
    if (color(code, &parsed)) {
        format = Text{.color = parsed, .hasColor = true};
        return true;
    }
    char lower = (code >= 'A' && code <= 'Z') ? static_cast<char>(code + 32) : code;
    switch (lower) {
        case 'l':
            format.bold = true;
            return true;
        case 'm':
            format.strike = true;
            return true;
        case 'n':
            format.underline = true;
            return true;
        case 'o':
            format.italic = true;
            return true;
        case 'r':
            format = Text{};
            return true;
        default:
            return false;
    }
}

inline auto is_code(const char* text, size_t remaining) -> bool {
    return remaining >= 3 && static_cast<unsigned char>(text[0]) == 0xC2 && static_cast<unsigned char>(text[1]) == 0xA7 && static_cast<unsigned char>(text[2]) < 0x80;
}

inline auto is_escape(const char* text, size_t remaining) -> bool {
    return remaining >= 4 && static_cast<unsigned char>(text[0]) == 0xC2 && static_cast<unsigned char>(text[1]) == 0xA7 && static_cast<unsigned char>(text[2]) == 0xC2 && static_cast<unsigned char>(text[3]) == 0xA7;
}

inline auto width(TTF_Font* normal, TTF_Font* italic, const char* text, size_t length, uint16_t fontSize, Text& state, bool editMode, int* outHeight) -> int {
    int width = 0;
    int height = 0;
    auto run = [&](const char* part, size_t count, const Text& format) -> void {
        if (count == 0) {
            return;
        }
        TTF_Font* font = (format.italic && italic != nullptr) ? italic : normal;
        TTF_SetFontSize(font, fontSize);
        int style = TTF_STYLE_NORMAL;
        if (format.bold) {
            style |= TTF_STYLE_BOLD;
        }
        if (format.italic && italic == nullptr) {
            style |= TTF_STYLE_ITALIC;
        }
        TTF_SetFontStyle(font, style);
        int w = 0;
        int h = 0;
        TTF_GetStringSize(font, part, count, &w, &h);
        TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
        width += w;
        height = std::max(height, h);
    };

    size_t start = 0;
    for (size_t i = 0; i < length;) {
        if (is_escape(text + i, length - i)) {
            run(text + start, i - start, state);
            run(text + i, editMode ? 4 : 2, state);
            i += 4;
            start = i;
        } else if (is_code(text + i, length - i)) {
            run(text + start, i - start, state);
            if (editMode) {
                run(text + i, 3, Text{});
            }
            apply(text[i + 2], state);
            i += 3;
            start = i;
        } else if (editMode && length - i >= 2 && static_cast<unsigned char>(text[i]) == 0xC2 && static_cast<unsigned char>(text[i + 1]) == 0xA7) {
            run(text + start, i - start, state);
            run(text + i, 2, Text{});
            i += 2;
            start = i;
        } else {
            ++i;
        }
    }
    run(text + start, length - start, state);
    if (outHeight != nullptr) {
        *outHeight = height;
    }
    return width;
}

inline auto trailing_bearing(TTF_Font* font, const char* text, size_t length) -> int {
    if (font == nullptr || length == 0) {
        return 0;
    }
    size_t i = length;
    do {
        --i;
    } while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80);
    unsigned char lead = static_cast<unsigned char>(text[i]);
    size_t avail = length - i;
    auto cont = [&](size_t k) -> uint32_t { return static_cast<unsigned char>(text[i + k]) & 0x3F; };
    uint32_t cp = lead;
    if (lead >= 0xF0 && avail >= 4) {
        cp = ((lead & 0x07U) << 18) | (cont(1) << 12) | (cont(2) << 6) | cont(3);
    } else if (lead >= 0xE0 && avail >= 3) {
        cp = ((lead & 0x0FU) << 12) | (cont(1) << 6) | cont(2);
    } else if (lead >= 0xC0 && avail >= 2) {
        cp = ((lead & 0x1FU) << 6) | cont(1);
    }
    int minx = 0;
    int maxx = 0;
    int miny = 0;
    int maxy = 0;
    int advance = 0;
    if (!TTF_GetGlyphMetrics(font, cp, &minx, &maxx, &miny, &maxy, &advance)) {
        return 0;
    }
    return std::max(0, advance - maxx);
}

}
