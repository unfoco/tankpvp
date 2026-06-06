#include "widget.h"

#include <string>

#include "util/format.h"

auto widget::wrap(InterfaceState& state, const std::string& text, uint16_t fontSize, float maxWidth) -> std::string {
    if (state.font == nullptr || maxWidth <= 0) {
        return text;
    }
    auto measure = [&](const std::string& s, format::Text start) -> float {
        int h = 0;
        return static_cast<float>(format::width(state.font, nullptr, s.data(), s.size(), fontSize, start, false, &h));
    };
    auto advance = [&](const std::string& s, format::Text& fmt) -> void {
        format::width(state.font, nullptr, s.data(), s.size(), fontSize, fmt, false, nullptr);
    };

    std::string out;
    std::string line;
    format::Text lineStart;
    auto commit = [&]() -> void {
        advance(line, lineStart);
        out += line;
        out += '\n';
        line.clear();
    };

    size_t i = 0;
    size_t n = text.size();

    while (i < n) {
        if (text[i] == '\n') {
            commit();
            ++i;
            continue;
        }

        if (text[i] == ' ') {
            if (!line.empty()) {
                if (measure(line + " ", lineStart) <= maxWidth) {
                    line += ' ';
                } else {
                    commit();
                }
            }
            ++i;
            continue;
        }

        size_t j = i;
        while (j < n && text[j] != ' ' && text[j] != '\n') {
            const char* q = &text[j];
            size_t rem = n - j;
            SDL_StepUTF8(&q, &rem);
            j += static_cast<size_t>(q - &text[j]);
        }
        std::string word = text.substr(i, j - i);

        if (measure(line + word, lineStart) <= maxWidth) {
            line += word;
            i = j;
            continue;
        }

        format::Text afterLine = lineStart;
        advance(line, afterLine);
        if (measure(word, afterLine) <= maxWidth && !line.empty() && measure(line, lineStart) >= maxWidth * 0.5F) {
            commit();
            line = word;
            i = j;
        } else {
            size_t k = i;
            while (k < j) {
                const char* q = &text[k];
                size_t rem = n - k;
                SDL_StepUTF8(&q, &rem);
                size_t cb = static_cast<size_t>(q - &text[k]);
                std::string cp = text.substr(k, cb);
                if (line.empty() || measure(line + cp, lineStart) <= maxWidth) {
                    line += cp;
                    k += cb;
                } else {
                    commit();
                }
            }
            i = j;
        }
    }

    out += line;
    return out;
}
