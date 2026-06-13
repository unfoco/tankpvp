#pragma once

#include <SDL3/SDL.h>

#include <stb/stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>


namespace format {

struct Rgba {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

inline constexpr uint16_t FONT_NORMAL = 0;
inline constexpr uint16_t FONT_ITALIC = 1;

struct Font {
    stbtt_fontinfo info{};
    std::vector<unsigned char> data;
    bool ok = false;

    auto load(const char* path) -> bool {
        size_t length = 0;
        void* bytes = SDL_LoadFile(path, &length);
        if (bytes == nullptr) {
            return false;
        }
        data.assign(static_cast<unsigned char*>(bytes), static_cast<unsigned char*>(bytes) + length);
        SDL_free(bytes);
        ok = stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)) != 0;
        return ok;
    }
};

inline auto utf8_next(const char* text, size_t remaining, uint32_t* cp) -> size_t {
    const char* p = text;
    size_t rem = remaining;
    *cp = SDL_StepUTF8(&p, &rem);
    return static_cast<size_t>(p - text);
}

inline void advance_glyph(const Font* font, uint16_t size, uint32_t cp, uint32_t& prev, float& w) {
    float scale = stbtt_ScaleForMappingEmToPixels(&font->info, static_cast<float>(size));
    int advance = 0;
    int lsb = 0;
    stbtt_GetCodepointHMetrics(&font->info, static_cast<int>(cp), &advance, &lsb);
    if (prev != 0) {
        w += static_cast<float>(stbtt_GetCodepointKernAdvance(&font->info, static_cast<int>(prev), static_cast<int>(cp))) * scale;
    }
    w += static_cast<float>(advance) * scale;
    prev = cp;
}

inline auto line_height(const Font* font, uint16_t size) -> int {
    if (font == nullptr || !font->ok) {
        return size;
    }
    float scale = stbtt_ScaleForMappingEmToPixels(&font->info, static_cast<float>(size));
    int ascent = 0;
    int descent = 0;
    int gap = 0;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &gap);
    int h = static_cast<int>(std::lround(static_cast<float>(ascent - descent) * scale));
    return h > 0 ? h : size;
}

inline auto width(const Font* font, const char* text, size_t length, uint16_t size, int* out_height = nullptr) -> int {
    if (out_height != nullptr) {
        *out_height = line_height(font, size);
    }
    if (font == nullptr || !font->ok || text == nullptr || length == 0) {
        return 0;
    }
    float w = 0.0F;
    uint32_t prev = 0;
    size_t i = 0;
    while (i < length) {
        uint32_t cp = 0;
        i += utf8_next(text + i, length - i, &cp);
        advance_glyph(font, size, cp, prev, w);
    }
    return static_cast<int>(std::lround(w));
}

inline auto has_glyph(const Font* font, uint32_t cp) -> bool {
    return font != nullptr && font->ok && stbtt_FindGlyphIndex(&font->info, static_cast<int>(cp)) != 0;
}

inline auto trailing_bearing(const Font* font, const char* text, size_t length, uint16_t size) -> int {
    if (font == nullptr || !font->ok || length == 0) {
        return 0;
    }
    size_t i = length;
    do {
        --i;
    } while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80);
    uint32_t cp = 0;
    utf8_next(text + i, length - i, &cp);
    int advance = 0;
    int lsb = 0;
    stbtt_GetCodepointHMetrics(&font->info, static_cast<int>(cp), &advance, &lsb);
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    if (stbtt_GetCodepointBox(&font->info, static_cast<int>(cp), &x0, &y0, &x1, &y1) == 0) {
        return 0;
    }
    float scale = stbtt_ScaleForMappingEmToPixels(&font->info, static_cast<float>(size));
    return std::max(0, static_cast<int>(std::lround(static_cast<float>(advance - x1) * scale)));
}


struct Text {
    Rgba color;
    bool hasColor = false;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
};

inline auto color(char code) -> std::optional<Rgba> {
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
            return Rgba{
                .r = static_cast<uint8_t>((entry.rgb >> 16) & 0xFF),
                .g = static_cast<uint8_t>((entry.rgb >> 8) & 0xFF),
                .b = static_cast<uint8_t>(entry.rgb & 0xFF),
                .a = 255,
            };
        }
    }
    return std::nullopt;
}

inline auto apply(char code, Text& format) -> bool {
    if (auto parsed = color(code)) {
        format = Text{.color = *parsed, .hasColor = true};
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

enum class Mode : uint8_t { Display, Edit };

struct Span {
    const char* data;
    size_t length;
    Text style;
    bool code;
};

template <class Emit>
inline auto scan(const char* text, size_t length, Mode mode, Emit&& emit, Text style = {}) -> Text {
    size_t rs = 0;
    size_t i = 0;
    auto flush = [&](size_t end) -> void {
        if (end > rs) {
            emit(Span{.data = text + rs, .length = end - rs, .style = style, .code = false});
        }
    };
    while (i < length) {
        if (mode == Mode::Display && is_escape(text + i, length - i)) {
            flush(i);
            emit(Span{.data = text + i, .length = 2, .style = style, .code = false});
            i += 4;
            rs = i;
            continue;
        }
        if (is_code(text + i, length - i)) {
            flush(i);
            if (mode == Mode::Edit) {
                emit(Span{.data = text + i, .length = 3, .style = style, .code = true});
            }
            apply(text[i + 2], style);
            i += 3;
            rs = i;
            continue;
        }
        uint32_t cp = 0;
        i += utf8_next(text + i, length - i, &cp);
    }
    flush(length);
    return style;
}

inline auto measure_rich(const Font* normal, const Font* italic, const char* text, size_t length, uint16_t size, Mode mode) -> int {
    if (normal == nullptr || !normal->ok || text == nullptr || length == 0) {
        return 0;
    }
    const Font* italicFont = (italic != nullptr && italic->ok) ? italic : normal;
    float w = 0.0F;
    scan(text, length, mode, [&](const Span& s) -> void {
        const Font* font = (!s.code && s.style.italic) ? italicFont : normal;
        uint32_t prev = 0;
        size_t k = 0;
        while (k < s.length) {
            uint32_t cp = 0;
            k += utf8_next(s.data + k, s.length - k, &cp);
            advance_glyph(font, size, cp, prev, w);
        }
    });
    return static_cast<int>(std::lround(w));
}

}
