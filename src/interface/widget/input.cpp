#include "widget.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "util/format.h"
#include "util/time.h"

static auto count_newlines(std::string_view s) -> int {
    int n = 0;
    for (char c : s) {
        if (c == '\n') {
            ++n;
        }
    }
    return n;
}

struct FieldCtx {
    InterfaceState& state;
    const WindowEvents& events;
    InputField& f;
    std::string& buf;
    const InputConfig& cfg;
    InputStyle& st;
    Clay_ElementId id;

    auto prevByte(size_t off) const -> size_t {
        const char* p = buf.c_str() + off;
        SDL_StepBackUTF8(buf.c_str(), &p);
        return static_cast<size_t>(p - buf.c_str());
    }
    auto nextByte(size_t off) const -> size_t {
        const char* p = buf.c_str() + off;
        size_t rem = buf.size() - off;
        SDL_StepUTF8(&p, &rem);
        return static_cast<size_t>(p - buf.c_str());
    }

    auto eraseSelection() -> bool {
        if (!f.hasSelection()) {
            return false;
        }
        buf.erase(f.selectionStart(), f.selectionEnd() - f.selectionStart());
        f.cursor = f.selectionStart();
        f.collapseSelection();
        return true;
    }
    void copySelection() const {
        if (!f.hasSelection()) {
            return;
        }
        std::string clip = buf.substr(f.selectionStart(), f.selectionEnd() - f.selectionStart());
        SDL_SetClipboardText(clip.c_str());
    }
    void insertFiltered(const char* text) {
        eraseSelection();
        const char* p = text;
        size_t remaining = SDL_strlen(text);
        while (remaining > 0) {
            const char* next = p;
            size_t rem = remaining;
            uint32_t cp = SDL_StepUTF8(&next, &rem);
            size_t cpLen = static_cast<size_t>(next - p);
            if (cfg.maxLength && SDL_utf8strlen(buf.c_str()) >= cfg.maxLength) {
                break;
            }

            bool ok = true;
            if (cfg.allowFn) {
                for (size_t i = 0; i < cpLen; ++i) {
                    if (!cfg.allowFn(p[i])) {
                        ok = false;
                        break;
                    }
                }
            } else if (cfg.allow) {
                for (size_t i = 0; i < cpLen; ++i) {
                    if (!cfg.allow(p[i])) {
                        ok = false;
                        break;
                    }
                }
            }

            if (ok) {
                bool regular = cp == ' ' || cp == '\n' || (cp >= 0x21 && cp <= 0x7E) || (cp >= 0xC0 && cp <= 0x24F);
                if (cfg.allowFormatting && cp == 0xA7) {
                    regular = true;
                }
                if (!regular || (state.font && !TTF_FontHasGlyph(state.font, cp))) {
                    ok = false;
                }
            }

            if (ok) {
                buf.insert(f.cursor, p, cpLen);
                f.cursor += cpLen;
            }
            p = next;
            remaining = rem;
        }
        f.collapseSelection();
    }

    auto measureLine() const -> int {
        if (!state.font) {
            return static_cast<int>(st.fontSize);
        }
        TTF_SetFontSize(state.font, st.fontSize);
        int w = 0;
        int h = 0;
        TTF_GetStringSize(state.font, "A", 1, &w, &h);
        return h > 0 ? h : static_cast<int>(st.fontSize);
    }
    auto lineWidth(const char* lineData, size_t byteEnd) const -> float {
        format::Text fmt;
        for (const char* p = buf.data(); p < lineData;) {
            if (format::is_escape(p, static_cast<size_t>(lineData - p))) {
                p += 4;
            } else if (format::is_code(p, static_cast<size_t>(lineData - p))) {
                format::apply(p[2], fmt);
                p += 3;
            } else {
                ++p;
            }
        }
        int h = 0;
        return static_cast<float>(format::width(state.font, nullptr, lineData, byteEnd, st.fontSize, fmt, cfg.allowFormatting, &h));
    }
    auto hitTest(float mx, float my) -> size_t {
        if (!state.font || buf.empty()) {
            return 0;
        }
        TTF_SetFontSize(state.font, st.fontSize);

        int lh = measureLine();
        float relX = std::max(0.F, mx - f.bounds.x - static_cast<float>(st.padding.left) + f.scrollX);
        float relY = std::max(0.F, my - f.bounds.y - static_cast<float>(st.padding.top) + f.scrollY);
        int targetLine = static_cast<int>(relY / static_cast<float>(lh));

        size_t pos = 0;
        int line = 0;
        while (true) {
            size_t nl = buf.find('\n', pos);
            size_t lineEnd = (nl == std::string::npos) ? buf.size() : nl;
            const char* lineData = buf.c_str() + pos;
            size_t lineBytes = lineEnd - pos;

            if (line == targetLine || nl == std::string::npos) {
                if (relX <= 0) {
                    return pos;
                }
                const char* p = lineData;
                size_t rem = lineBytes;
                float prevW = 0;
                size_t prevOff = 0;
                while (rem > 0) {
                    const char* q = p;
                    size_t r = rem;
                    SDL_StepUTF8(&q, &r);
                    size_t byteInLine = static_cast<size_t>(q - lineData);
                    float w = lineWidth(lineData, byteInLine);
                    if (w >= relX) {
                        return pos + ((w - relX < relX - prevW) ? byteInLine : prevOff);
                    }
                    prevW = w;
                    prevOff = byteInLine;
                    p = q;
                    rem = r;
                }
                return pos + lineBytes;
            }

            pos = nl + 1;
            ++line;
        }
    }

    auto handleKeys(bool numeric, const std::function<std::string()>& reformat) -> bool {
        bool enterCommit = false;
        for (const auto& e : events) {
            if (e.type == SDL_EVENT_TEXT_INPUT) {
                f.blinkBase = util::now();
                insertFiltered(e.text.text);
                continue;
            }
            if (e.type != SDL_EVENT_KEY_DOWN) {
                continue;
            }
            f.blinkBase = util::now();

            bool mod = (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
            bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
            auto key = e.key.key;

            bool repeatable = (key == SDLK_BACKSPACE || key == SDLK_DELETE || key == SDLK_LEFT || key == SDLK_RIGHT);
            if (e.key.repeat && !repeatable) {
                continue;
            }

            if (key == SDLK_LEFT) {
                if (mod) {
                    f.cursor = 0;
                } else if (!shift && f.hasSelection()) {
                    f.cursor = f.selectionStart();
                } else if (f.cursor > 0) {
                    f.cursor = prevByte(f.cursor);
                }
                if (!shift) {
                    f.collapseSelection();
                }
                continue;
            }
            if (key == SDLK_RIGHT) {
                if (mod) {
                    f.cursor = buf.size();
                } else if (!shift && f.hasSelection()) {
                    f.cursor = f.selectionEnd();
                } else if (f.cursor < buf.size()) {
                    f.cursor = nextByte(f.cursor);
                }
                if (!shift) {
                    f.collapseSelection();
                }
                continue;
            }
            if (key == SDLK_HOME) {
                f.cursor = 0;
                if (!shift) {
                    f.collapseSelection();
                }
                continue;
            }
            if (key == SDLK_END) {
                f.cursor = buf.size();
                if (!shift) {
                    f.collapseSelection();
                }
                continue;
            }

            if (key == SDLK_BACKSPACE) {
                if (!eraseSelection() && f.cursor > 0) {
                    size_t prev = prevByte(f.cursor);
                    buf.erase(prev, f.cursor - prev);
                    f.cursor = prev;
                    f.collapseSelection();
                }
                continue;
            }
            if (key == SDLK_DELETE) {
                if (!eraseSelection() && f.cursor < buf.size()) {
                    buf.erase(f.cursor, nextByte(f.cursor) - f.cursor);
                    f.collapseSelection();
                }
                continue;
            }

            if (mod && key == SDLK_A) {
                f.anchor = 0;
                f.cursor = buf.size();
                continue;
            }
            if (mod && key == SDLK_C) {
                copySelection();
                continue;
            }
            if (mod && key == SDLK_X) {
                copySelection();
                eraseSelection();
                continue;
            }
            if (mod && key == SDLK_V) {
                if (const char* cb = SDL_GetClipboardText()) {
                    insertFiltered(cb);
                }
                continue;
            }

            if (key == SDLK_ESCAPE) {
                if (numeric) {
                    buf = reformat();
                }
                state.focusedId = 0;
                SDL_StopTextInput(events.target);
                continue;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if (!numeric && cfg.multiline) {
                    int lines = count_newlines(buf) + 1;
                    if (!cfg.maxLine || std::cmp_less(lines, cfg.maxLine)) {
                        eraseSelection();
                        buf.insert(f.cursor, "\n", 1);
                        f.cursor += 1;
                        f.collapseSelection();
                    }
                } else if (numeric || cfg.commitOnEnter) {
                    enterCommit = true;
                }
                continue;
            }
            if (!numeric && key == SDLK_TAB && cfg.multiline) {
                eraseSelection();
                buf.insert(f.cursor, "\t", 1);
                f.cursor += 1;
                f.collapseSelection();
                continue;
            }
        }
        if (enterCommit) {
            state.focusedId = 0;
            SDL_StopTextInput(events.target);
        }
        return enterCommit;
    }

    auto draw(bool focused) -> bool {
        bool empty = buf.empty();
        Clay_Sizing sizing = st.sizing;
        Clay_Color border = focused ? st.focusBorderColor : st.borderColor;
        int lh = measureLine();

        if (!cfg.multiline) {
            sizing.height = CLAY_SIZING_FIXED((float)st.fontSize + st.padding.top + st.padding.bottom);
        }

        float cursorPx = 0;
        float cursorPy = 0;
        if (focused && state.font && f.cursor > 0) {
            std::string_view textToCursor(buf.data(), f.cursor);
            auto lastNl = textToCursor.rfind('\n');
            const char* lineStart = (lastNl == std::string_view::npos) ? textToCursor.data() : textToCursor.data() + lastNl + 1;
            size_t lineBytes = (lastNl == std::string_view::npos) ? textToCursor.size() : textToCursor.size() - lastNl - 1;

            cursorPx = lineWidth(lineStart, lineBytes);
            cursorPy = static_cast<float>(count_newlines(textToCursor) * lh);
        }

        int totalLines = count_newlines(buf) + 1;

        if (cfg.multiline) {
            int displayLines = (cfg.maxHeight > 0) ? std::min(totalLines, static_cast<int>(cfg.maxHeight)) : totalLines;
            float h = static_cast<float>(displayLines * lh) + static_cast<float>(st.padding.top) + static_cast<float>(st.padding.bottom);
            sizing.height = CLAY_SIZING_FIT();
            sizing.height.size.minMax.min = h;
            if (cfg.maxHeight > 0) {
                sizing.height.size.minMax.max = static_cast<float>(static_cast<int>(cfg.maxHeight) * lh) + static_cast<float>(st.padding.top) + static_cast<float>(st.padding.bottom);
            }
        }

        if (focused) {
            float contentW = (f.bounds.width > 0) ? f.bounds.width - static_cast<float>(st.padding.left) - static_cast<float>(st.padding.right) : 0;

            float contentH;
            if (!cfg.multiline) {
                contentH = static_cast<float>(st.fontSize);
            } else if (cfg.maxHeight > 0) {
                contentH = static_cast<float>(static_cast<int>(cfg.maxHeight) * lh);
            } else {
                contentH = static_cast<float>(totalLines * lh);
            }

            if (contentW > 0) {
                if (cursorPx - f.scrollX > contentW) {
                    f.scrollX = cursorPx - contentW;
                }
                if (cursorPx - f.scrollX < 0) {
                    f.scrollX = cursorPx;
                }
                f.scrollX = std::max<float>(f.scrollX, 0);
            }
            if (cfg.multiline && contentH > 0) {
                float cursorBottom = cursorPy + static_cast<float>(lh);
                if (cursorBottom - f.scrollY > contentH) {
                    f.scrollY = cursorBottom - contentH;
                }
                if (cursorPy - f.scrollY < 0) {
                    f.scrollY = cursorPy;
                }

                float maxScroll = std::max(0.F, static_cast<float>(totalLines * lh) - contentH);
                f.scrollY = std::clamp(f.scrollY, 0.F, maxScroll);
            } else {
                f.scrollY = 0;
            }
        }

        bool clicked = false;
        bool blinkOn = std::fmod(util::now() - f.blinkBase, 1.6) < 0.8;

        CLAY({
            .id = id,
            .layout =
                {
                    .sizing = sizing,
                    .padding = st.padding,
                    .childAlignment = {.y = cfg.multiline ? CLAY_ALIGN_Y_TOP : CLAY_ALIGN_Y_CENTER},
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                },
            .backgroundColor = st.bgColor,
            .cornerRadius = CLAY_CORNER_RADIUS(st.cornerRadius),
            .clip = {.horizontal = true, .vertical = cfg.multiline, .childOffset = {-f.scrollX, -f.scrollY}},
        }) {
            CLAY({
                .layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()}},
                .cornerRadius = CLAY_CORNER_RADIUS(st.cornerRadius),
                .floating =
                    {
                        .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                        .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                        .attachTo = CLAY_ATTACH_TO_PARENT,
                    },
                .border = {.color = border, .width = {st.borderWidth, st.borderWidth, st.borderWidth, st.borderWidth}},
            }) {}

            if (Clay_Hovered() && !cfg.disabled) {
                state.cursor = InterfaceCursor::Text;
                if (state.mousePressed) {
                    state.focusedId = id.id;
                    state.focusConsumed = true;
                    clicked = true;
                    SDL_StartTextInput(events.target);
                    f.cursor = hitTest(state.mouseX, state.mouseY);
                    f.anchor = f.cursor;
                    f.blinkBase = util::now();
                }
            }

            if (empty) {
                CLAY_TEXT(Str(cfg.placeholder), CLAY_TEXT_CONFIG({
                                                    .textColor = st.placeholderColor,
                                                    .fontSize = st.fontSize,
                                                    .wrapMode = CLAY_TEXT_WRAP_NONE,
                                                }));

                if (focused && blinkOn) {
                    float ox = static_cast<float>(st.padding.left);
                    float centerY = cfg.multiline ? 0.F : (static_cast<float>(st.fontSize) - static_cast<float>(lh)) / 2.F;
                    float oy = static_cast<float>(st.padding.top) + centerY;

                    CLAY({
                        .layout = {.sizing = {CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED((float)lh)}},
                        .backgroundColor = st.textColor,
                        .floating =
                            {
                                .offset = {ox, oy},
                                .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                                .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                                .attachTo = CLAY_ATTACH_TO_PARENT,
                                .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT,
                            },
                    }) {}
                }
            } else {
                uint16_t textFontId = (cfg.allowFormatting && focused) ? format::FONT_EDIT : 0;
                if (cfg.multiline) {
                    float ww = (f.bounds.width > 0) ? f.bounds.width - static_cast<float>(st.padding.left) - static_cast<float>(st.padding.right) : 0;
                    const std::string& disp = state.intern(widget::wrap(state, buf, st.fontSize, ww));
                    CLAY_TEXT(Str(disp), CLAY_TEXT_CONFIG({
                                             .textColor = st.textColor,
                                             .fontId = textFontId,
                                             .fontSize = st.fontSize,
                                             .wrapMode = CLAY_TEXT_WRAP_NEWLINES,
                                         }));
                } else {
                    CLAY_TEXT(Str(buf), CLAY_TEXT_CONFIG({
                                            .textColor = st.textColor,
                                            .fontId = textFontId,
                                            .fontSize = st.fontSize,
                                            .wrapMode = CLAY_TEXT_WRAP_NONE,
                                        }));
                }

                if (focused) {
                    float ox = static_cast<float>(st.padding.left) - f.scrollX;
                    float centerY = cfg.multiline ? 0.F : (static_cast<float>(st.fontSize) - static_cast<float>(lh)) / 2.F;
                    float oy = static_cast<float>(st.padding.top) + centerY - f.scrollY;

                    if (f.hasSelection()) {
                        size_t lo = f.selectionStart();
                        size_t hi = f.selectionEnd();
                        size_t pos = 0;
                        int line = 0;
                        while (true) {
                            size_t nl = buf.find('\n', pos);
                            size_t lineEnd = (nl == std::string::npos) ? buf.size() : nl;
                            const char* lineData = buf.c_str() + pos;

                            if (lo <= lineEnd && hi > pos) {
                                size_t selS = std::max(lo, pos);
                                size_t selE = std::min(hi, lineEnd);
                                float xStart = 0;
                                if (selS > pos) {
                                    xStart = lineWidth(lineData, selS - pos);
                                }
                                size_t selEbytes = selE - pos;
                                TTF_SetFontSize(state.font, st.fontSize);
                                float bearing = 0;
                                if (selEbytes > 0 && lineData[selEbytes - 1] != ' ' && lineData[selEbytes - 1] != '\t') {
                                    bearing = static_cast<float>(format::trailing_bearing(state.font, lineData, selEbytes));
                                }
                                float wsel = lineWidth(lineData, selEbytes) - bearing;
                                float selW = wsel - xStart;
                                if (selW < 1.0F) {
                                    selW = static_cast<float>(st.fontSize) / 3.0F;
                                }

                                CLAY({
                                    .layout = {.sizing = {CLAY_SIZING_FIXED(selW), CLAY_SIZING_FIXED((float)lh)}},
                                    .backgroundColor = {70, 130, 255, 128},
                                    .floating =
                                        {
                                            .offset = {ox + xStart, oy + (float)(line * lh)},
                                            .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                                            .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                                            .attachTo = CLAY_ATTACH_TO_PARENT,
                                            .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT,
                                        },
                                }) {}
                            }

                            if (nl == std::string::npos) {
                                break;
                            }
                            pos = nl + 1;
                            ++line;
                        }
                    } else if (blinkOn) {
                        CLAY({
                            .layout = {.sizing = {CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED((float)lh)}},
                            .backgroundColor = st.textColor,
                            .floating =
                                {
                                    .offset = {ox + cursorPx, oy + cursorPy},
                                    .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                                    .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                                    .attachTo = CLAY_ATTACH_TO_PARENT,
                                    .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT,
                                },
                        }) {}
                    }
                }
            }
        }

        return clicked;
    }
};

template <typename T>
auto widget::input(InterfaceState& state, const WindowEvents& events, Clay_ElementId id, T& value, InputConfig cfg, InputStyle st) -> bool {
    constexpr bool kNumeric = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;
    constexpr bool kString = std::is_same_v<T, std::string>;
    static_assert(kNumeric || kString, "T must be std::string or a numeric type");

    const char* fmt = cfg.format;
    if constexpr (std::is_floating_point_v<T>) {
        if (!fmt) {
            fmt = "%.2f";
        }
        if (!cfg.allow) {
            cfg.allow = InputFilter::Float;
        }
    } else if constexpr (std::is_integral_v<T>) {
        if (!fmt) {
            fmt = "%d";
        }
        if (!cfg.allow) {
            cfg.allow = InputFilter::Signed;
        }
    }

    auto formatValue = [&](auto v) -> std::string {
        if constexpr (!kNumeric) {
            return {};
        }
        char tmp[64];
        int n;
        if constexpr (std::is_floating_point_v<decltype(v)>) {
            n = snprintf(tmp, sizeof(tmp), fmt, static_cast<double>(v));
        } else {
            n = snprintf(tmp, sizeof(tmp), fmt, static_cast<long long>(v));
        }
        return {tmp, static_cast<size_t>(n > 0 ? n : 0)};
    };

    auto& f = state.acquireField(id.id);
    std::string& buf = [&]() -> std::string& {
        if constexpr (kString) {
            return value;
        } else {
            return f.editBuf;
        }
    }();

    f.cursor = std::min(f.cursor, buf.size());
    f.anchor = std::min(f.anchor, buf.size());

    if (cfg.disabled) {
        st.textColor.a *= 0.4F;
        st.bgColor.r *= 0.6F;
        st.bgColor.g *= 0.6F;
        st.bgColor.b *= 0.6F;
    }

    FieldCtx ctx{state, events, f, buf, cfg, st, id};

    bool focused = (state.focusedId == id.id) && !cfg.disabled;
    bool focusGain = focused && (state.prevFocusedId != id.id);
    bool focusLose = !focused && (state.prevFocusedId == id.id);

    if (focusGain) {
        if constexpr (kNumeric) {
            buf = formatValue(value);
        }
        f.cursor = buf.size();
        f.anchor = f.cursor;
        f.blinkBase = util::now();
    }

    auto parse = [&]() -> void {
        if constexpr (kNumeric) {
            char* end = nullptr;
            if constexpr (std::is_floating_point_v<T>) {
                double d = strtod(buf.c_str(), &end);
                if (end != buf.c_str()) {
                    value = std::clamp(static_cast<T>(d), static_cast<T>(cfg.min), static_cast<T>(cfg.max));
                }
            } else {
                long long ll = strtoll(buf.c_str(), &end, 10);
                if (end != buf.c_str()) {
                    value = std::clamp(static_cast<T>(ll), static_cast<T>(cfg.min), static_cast<T>(cfg.max));
                }
            }
        }
    };

    bool enterCommit = false;
    if (focused) {
        enterCommit = ctx.handleKeys(kNumeric, [&]() -> std::string {
            if constexpr (kNumeric) {
                return formatValue(value);
            } else {
                return {};
            }
        });
    }

    bool committed = false;
    if constexpr (kNumeric) {
        if (focused && !buf.empty()) {
            parse();
        }
        if (focusLose || enterCommit) {
            parse();
            buf = formatValue(value);
            if (value == T{} && cfg.placeholder[0]) {
                buf.clear();
            }
            committed = true;
        }
        if (!focused && !focusLose) {
            if (value == T{} && cfg.placeholder[0]) {
                buf.clear();
            } else {
                buf = formatValue(value);
            }
        }
    } else {
        if ((focusLose || enterCommit) && cfg.validate) {
            committed = cfg.validate(value);
        }
    }

    if (focusLose) {
        f.scrollX = 0;
        f.scrollY = 0;
    }

    if (state.mouseDown && !state.mousePressed && state.focusedId == id.id && state.font) {
        f.cursor = ctx.hitTest(state.mouseX, state.mouseY);
    }

    bool clicked = ctx.draw(focused);

    if constexpr (kNumeric) {
        return committed;
    } else {
        return clicked;
    }
}

template bool widget::input<std::string>(InterfaceState&, const WindowEvents&, Clay_ElementId, std::string&, InputConfig, InputStyle);
template bool widget::input<int>(InterfaceState&, const WindowEvents&, Clay_ElementId, int&, InputConfig, InputStyle);
template bool widget::input<uint16_t>(InterfaceState&, const WindowEvents&, Clay_ElementId, uint16_t&, InputConfig, InputStyle);
template bool widget::input<float>(InterfaceState&, const WindowEvents&, Clay_ElementId, float&, InputConfig, InputStyle);
template bool widget::input<double>(InterfaceState&, const WindowEvents&, Clay_ElementId, double&, InputConfig, InputStyle);
