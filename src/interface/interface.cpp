#include <string_view>
#include <algorithm>
#include <string>

#include "interface.h"

static inline size_t Utf8CpBytes(unsigned char lead) noexcept {
    if (lead < 0x80) return 1;
    if (lead < 0xE0) return 2;
    if (lead < 0xF0) return 3;
    return 4;
}

static inline size_t Utf8Len(const char* s, size_t bytes) noexcept {
    size_t n = 0;
    for (size_t i = 0; i < bytes; ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    }
    return n;
}

static inline size_t Utf8Len(const std::string& s) noexcept {
    return Utf8Len(s.data(), s.size());
}

static inline size_t Utf8ToBytes(const char* s, size_t bytes, size_t cp) noexcept {
    size_t off = 0;
    for (size_t i = 0; i < cp && off < bytes; ++i)
        off += Utf8CpBytes(static_cast<unsigned char>(s[off]));
    return off;
}

static inline size_t Utf8ToBytes(const std::string& s, size_t cp) noexcept {
    return Utf8ToBytes(s.data(), s.size(), cp);
}

static inline void Utf8Erase(std::string& s, size_t cpStart, size_t cpEnd) {
    size_t a = Utf8ToBytes(s, cpStart), b = Utf8ToBytes(s, cpEnd);
    s.erase(a, b - a);
}

static inline void Utf8Insert(std::string& s, size_t cpPos, const char* text, size_t len) {
    s.insert(Utf8ToBytes(s, cpPos), text, len);
}

Interface::Interface(flecs::world& world) {
    world.component<InterfaceState>().add(flecs::Singleton);
    world.component<InterfacePage>().add(flecs::Singleton);
    world.component<InterfacePrevious>().add(flecs::Singleton);
    world.component<InterfaceCommands>().add(flecs::Singleton);

    TTF_Init();
    auto* font = TTF_OpenFont("asset/font.ttf", 16);
    if (!font) {
        SDL_Log("Failed to load font: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    world.set<InterfaceState>({.font = font});
    world.set<InterfaceCommands>({});
    world.set(InterfacePage::Main);
    world.set<InterfacePrevious>({.page = InterfacePage::Main});

    world.system<InterfaceState>("interface::frame")
        .kind(flecs::PreFrame).each(Interface::frame);
    world.system<InterfaceState, WindowEvents>("interface::event")
        .kind(flecs::PreUpdate).each(Interface::event);
    world.system<InterfaceState, InterfaceCommands, InterfacePage, InterfacePrevious, WindowEvents>("interface::build")
        .kind(flecs::OnUpdate).each(Interface::build);
}

void Interface::frame(flecs::iter&, size_t, InterfaceState& state) {
    state.prevFocusedId = state.focusedId;
    state.mousePressed  = false;
    state.focusConsumed = false;
}

void Interface::event(flecs::iter&, size_t, InterfaceState& state, const WindowEvents& events) {
    for (const auto& e : events) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            state.mousePressed = true;
            state.mouseDown    = true;
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            state.mouseDown = false;
            state.activeId  = 0;
        } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
            state.mouseX = e.motion.x;
            state.mouseY = e.motion.y;
        }
    }
}

void Interface::build(flecs::iter& it, size_t, InterfaceState& state, InterfaceCommands& cmds, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) {
    switch (page) {
    case InterfacePage::Main:     cmds.list = Interface::main(it, state, page, prev, events);     break;
    case InterfacePage::Pause:    cmds.list = Interface::pause(it, state, page, prev, events);    break;
    case InterfacePage::Ingame:   cmds.list = Interface::ingame(it, state, page, prev, events);   break;
    case InterfacePage::Connect:  cmds.list = Interface::connect(it, state, page, prev, events);  break;
    case InterfacePage::Settings: cmds.list = Interface::settings(it, state, page, prev, events); break;
    case InterfacePage::None:     cmds.list = {};                                             break;
    }

    for (int32_t i = 0; i < cmds.list.length; ++i) {
        auto* cmd = Clay_RenderCommandArray_Get(&cmds.list, i);
        if (auto* f = state.findField(cmd->id)) f->bounds = cmd->boundingBox;
    }

    if (state.mousePressed && !state.focusConsumed && state.focusedId != 0) {
        state.focusedId = 0;
        SDL_StopTextInput(events.target);
    }
}

bool Interface::button(InterfaceState& state, Clay_ElementId id, const char* label, ButtonStyle st) {
    bool clicked = false;
    CLAY({
        .id = id,
        .layout = {.padding = st.padding},
        .backgroundColor = st.color,
        .cornerRadius = CLAY_CORNER_RADIUS(st.cornerRadius),
    }) {
        if (Clay_Hovered() && state.mousePressed) clicked = true;
        CLAY_TEXT(Str(label), CLAY_TEXT_CONFIG({
            .textColor = st.textColor, .fontSize = st.fontSize,
        }));
    }
    return clicked;
}

bool Interface::toggle(InterfaceState& state, Clay_ElementId id, bool& value, ToggleStyle st) {
    bool toggled = false;
    float knob = st.height - st.knobPad * 2;
    uint16_t pad = (uint16_t)st.knobPad;

    CLAY({
        .id = id,
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(st.width), CLAY_SIZING_FIXED(st.height)},
            .padding = {pad, pad, pad, pad},
            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
        },
        .backgroundColor = value ? st.onColor : st.offColor,
        .cornerRadius = CLAY_CORNER_RADIUS(st.height / 2),
    }) {
        if (Clay_Hovered() && state.mousePressed) { value = !value; toggled = true; }
        if (value) CLAY({.layout = {.sizing = {CLAY_SIZING_GROW()}}}) {}
        CLAY({
            .layout = {.sizing = {CLAY_SIZING_FIXED(knob), CLAY_SIZING_FIXED(knob)}},
            .backgroundColor = st.knobColor,
            .cornerRadius = CLAY_CORNER_RADIUS(knob / 2),
        }) {}
    }
    return toggled;
}

bool Interface::slider(InterfaceState& state, Clay_ElementId id, float& value, float lo, float hi, SliderStyle st) {
    float prev = value, range = hi - lo;
    if (range <= 0) return false;

    if (state.activeId == id.id && state.mouseDown) {
        float delta = (state.mouseX - state.dragOriginX) / st.trackWidth * range;
        value = std::clamp(state.dragOriginValue + delta, lo, hi);
    }

    float norm = std::clamp((value - lo) / range, 0.f, 1.f);
    float fill = norm * st.trackWidth;

    CLAY({
        .id = id,
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(st.trackWidth), CLAY_SIZING_FIXED(st.trackHeight + 14)},
            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
        },
    }) {
        if (Clay_Hovered() && state.mousePressed) {
            state.activeId = id.id;
            state.dragOriginX = state.mouseX;
            state.dragOriginValue = value;
        }
        CLAY({
            .layout = {
                .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIXED(st.trackHeight)},
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
            .backgroundColor = st.trackColor,
            .cornerRadius = CLAY_CORNER_RADIUS(st.trackHeight / 2),
        }) {
            if (fill > 0.5f) {
                CLAY({
                    .layout = {.sizing = {CLAY_SIZING_FIXED(fill), CLAY_SIZING_GROW()}},
                    .backgroundColor = st.fillColor,
                    .cornerRadius = CLAY_CORNER_RADIUS(st.trackHeight / 2),
                }) {}
            }
        }
    }
    return value != prev;
}

template<typename T>
bool Interface::input(InterfaceState& state, const WindowEvents& events, Clay_ElementId id, T& value, InputConfig cfg, InputStyle st) {
    constexpr bool kNumeric = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;
    constexpr bool kString  = std::is_same_v<T, std::string>;
    static_assert(kNumeric || kString, "T must be std::string or a numeric type");

    const char* fmt = cfg.format;
    if constexpr (std::is_floating_point_v<T>) {
        if (!fmt) fmt = "%.2f";
        if (!cfg.allow) cfg.allow = InputFilter::Float;
    } else if constexpr (std::is_integral_v<T>) {
        if (!fmt) fmt = "%d";
        if (!cfg.allow) cfg.allow = InputFilter::Signed;
    }

    auto formatValue = [&](auto v) -> std::string {
        if constexpr (!kNumeric) return {};
        char tmp[64];
        int n;
        if constexpr (std::is_floating_point_v<decltype(v)>)
            n = snprintf(tmp, sizeof(tmp), fmt, (double)v);
        else
            n = snprintf(tmp, sizeof(tmp), fmt, (long long)v);
        return {tmp, static_cast<size_t>(n > 0 ? n : 0)};
    };

    auto& f = state.acquireField(id.id);
    std::string& buf = [&]() -> std::string& {
        if constexpr (kString) return value;
        else return f.editBuf;
    }();

    size_t len = Utf8Len(buf);
    if (f.cursor > len) f.cursor = len;
    if (f.anchor > len) f.anchor = len;

    auto eraseSelection = [&]() -> bool {
        if (!f.hasSelection()) return false;
        Utf8Erase(buf, f.selectionStart(), f.selectionEnd());
        f.cursor = f.selectionStart();
        f.collapseSelection();
        return true;
    };

    auto copySelection = [&]() {
        if (!f.hasSelection()) return;
        size_t a = Utf8ToBytes(buf, f.selectionStart());
        size_t b = Utf8ToBytes(buf, f.selectionEnd());
        std::string clip = buf.substr(a, b - a);
        SDL_SetClipboardText(clip.c_str());
    };

    auto insertFiltered = [&](const char* text) {
        eraseSelection();
        for (const char* p = text; *p;) {
            size_t cpLen = Utf8CpBytes(static_cast<unsigned char>(*p));
            if (cfg.maxLength && Utf8Len(buf) >= cfg.maxLength) break;

            bool ok = true;
            if (cfg.allow) {
                for (size_t i = 0; i < cpLen && p[i]; ++i)
                    if (!cfg.allow(p[i])) { ok = false; break; }
            }

            if (ok) { Utf8Insert(buf, f.cursor, p, cpLen); ++f.cursor; }
            p += cpLen;
        }
        f.collapseSelection();
    };

    auto measureLine = [&]() -> int {
        if (!state.font) return (int)st.fontSize;
        TTF_SetFontSize(state.font, st.fontSize);
        int w = 0, h = 0;
        TTF_GetStringSize(state.font, "A", 1, &w, &h);
        return h > 0 ? h : (int)st.fontSize;
    };

    auto hitTest = [&](float mx, float my) -> size_t {
        if (!state.font || buf.empty()) return 0;
        TTF_SetFontSize(state.font, st.fontSize);

        int lh = measureLine();
        float relX = std::max(0.f, mx - f.bounds.x - (float)st.padding.left + f.scrollX);
        float relY = std::max(0.f, my - f.bounds.y - (float)st.padding.top  + f.scrollY);
        int targetLine = (int)(relY / (float)lh);

        size_t cpOff = 0, pos = 0;
        int line = 0;
        while (true) {
            size_t nl = buf.find('\n', pos);
            size_t lineEnd = (nl == std::string::npos) ? buf.size() : nl;
            const char* lineData = buf.c_str() + pos;
            size_t lineBytes = lineEnd - pos;
            size_t lineCP = Utf8Len(lineData, lineBytes);

            if (line == targetLine || nl == std::string::npos) {
                if (relX <= 0) return cpOff;
                for (size_t i = 1; i <= lineCP; ++i) {
                    int w = 0, h = 0;
                    TTF_GetStringSize(state.font, lineData, Utf8ToBytes(lineData, lineBytes, i), &w, &h);
                    if ((float)w >= relX) {
                        int pw = 0;
                        TTF_GetStringSize(state.font, lineData, Utf8ToBytes(lineData, lineBytes, i - 1), &pw, &h);
                        return cpOff + (((float)w - relX < relX - (float)pw) ? i : i - 1);
                    }
                }
                return cpOff + lineCP;
            }

            cpOff += lineCP + 1;
            pos = nl + 1;
            ++line;
        }
    };

    bool focused   = (state.focusedId == id.id);
    bool focusGain = focused && (state.prevFocusedId != id.id);
    bool focusLose = !focused && (state.prevFocusedId == id.id);

    if (focusGain) {
        if constexpr (kNumeric) buf = formatValue(value);
        f.cursor = Utf8Len(buf);
        f.anchor = f.cursor;
    }

    bool enterCommit = false;

    if (focused) {
        for (const auto& e : events) {
            if (e.type == SDL_EVENT_TEXT_INPUT) {
                insertFiltered(e.text.text);
                continue;
            }
            if (e.type != SDL_EVENT_KEY_DOWN) continue;

            bool mod   = (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
            bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
            auto key   = e.key.key;

            bool repeatable = (key == SDLK_BACKSPACE || key == SDLK_DELETE || key == SDLK_LEFT || key == SDLK_RIGHT);
            if (e.key.repeat && !repeatable) continue;

            if (key == SDLK_LEFT) {
                if (mod) f.cursor = 0;
                else if (!shift && f.hasSelection()) f.cursor = f.selectionStart();
                else if (f.cursor > 0) --f.cursor;
                if (!shift) f.collapseSelection();
                continue;
            }
            if (key == SDLK_RIGHT) {
                size_t l = Utf8Len(buf);
                if (mod) f.cursor = l;
                else if (!shift && f.hasSelection()) f.cursor = f.selectionEnd();
                else if (f.cursor < l) ++f.cursor;
                if (!shift) f.collapseSelection();
                continue;
            }
            if (key == SDLK_HOME) { f.cursor = 0; if (!shift) f.collapseSelection(); continue; }
            if (key == SDLK_END)  { f.cursor = Utf8Len(buf); if (!shift) f.collapseSelection(); continue; }

            if (key == SDLK_BACKSPACE) {
                if (!eraseSelection() && f.cursor > 0) {
                    --f.cursor;
                    Utf8Erase(buf, f.cursor, f.cursor + 1);
                    f.collapseSelection();
                }
                continue;
            }
            if (key == SDLK_DELETE) {
                if (!eraseSelection() && f.cursor < Utf8Len(buf)) {
                    Utf8Erase(buf, f.cursor, f.cursor + 1);
                    f.collapseSelection();
                }
                continue;
            }

            if (mod && key == SDLK_A) { f.anchor = 0; f.cursor = Utf8Len(buf); continue; }
            if (mod && key == SDLK_C) { copySelection(); continue; }
            if (mod && key == SDLK_X) { copySelection(); eraseSelection(); continue; }
            if (mod && key == SDLK_V) {
                if (const char* cb = SDL_GetClipboardText()) insertFiltered(cb);
                continue;
            }

            if (key == SDLK_ESCAPE) {
                if constexpr (kNumeric) buf = formatValue(value);
                state.focusedId = 0;
                SDL_StopTextInput(events.target);
                continue;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if constexpr (kNumeric) {
                    enterCommit = true;
                } else if (cfg.multiline) {
                    int lines = 1;
                    for (char c : buf) if (c == '\n') ++lines;
                    if (!cfg.maxLine || lines < (int)cfg.maxLine) {
                        eraseSelection();
                        Utf8Insert(buf, f.cursor, "\n", 1);
                        ++f.cursor;
                        f.collapseSelection();
                    }
                } else if (cfg.commitOnEnter) {
                    enterCommit = true;
                }
                continue;
            }
            if constexpr (kString) {
                if (key == SDLK_TAB && cfg.multiline) {
                    eraseSelection();
                    Utf8Insert(buf, f.cursor, "\t", 1);
                    ++f.cursor;
                    f.collapseSelection();
                    continue;
                }
            }
        }
        if (enterCommit) {
            state.focusedId = 0;
            SDL_StopTextInput(events.target);
        }
    }

    if constexpr (kNumeric) {
        if (focused && !buf.empty()) {
            char* end = nullptr;
            if constexpr (std::is_floating_point_v<T>) {
                double d = strtod(buf.c_str(), &end);
                if (end != buf.c_str()) value = std::clamp(static_cast<T>(d), static_cast<T>(cfg.min), static_cast<T>(cfg.max));
            } else {
                long long ll = strtoll(buf.c_str(), &end, 10);
                if (end != buf.c_str()) value = std::clamp(static_cast<T>(ll), static_cast<T>(cfg.min), static_cast<T>(cfg.max));
            }
        }
    }

    bool committed = false;
    if constexpr (kNumeric) {
        if (focusLose || enterCommit) {
            char* end = nullptr;
            if constexpr (std::is_floating_point_v<T>) {
                double d = strtod(buf.c_str(), &end);
                if (end != buf.c_str()) value = std::clamp(static_cast<T>(d), static_cast<T>(cfg.min), static_cast<T>(cfg.max));
            } else {
                long long ll = strtoll(buf.c_str(), &end, 10);
                if (end != buf.c_str()) value = std::clamp(static_cast<T>(ll), static_cast<T>(cfg.min), static_cast<T>(cfg.max));
            }
            buf = formatValue(value);
            if (value == T{} && cfg.placeholder[0]) buf.clear();
            committed = true;
        }
        if (!focused && !focusLose) {
            if (value == T{} && cfg.placeholder[0]) buf.clear();
            else buf = formatValue(value);
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

    if (state.mouseDown && !state.mousePressed && state.focusedId == id.id && state.font)
        f.cursor = hitTest(state.mouseX, state.mouseY);

    bool empty = buf.empty();
    Clay_Sizing sizing = st.sizing;
    Clay_Color border = focused ? st.focusBorderColor : st.borderColor;
    int lh = measureLine();

    if (!cfg.multiline)
        sizing.height = CLAY_SIZING_FIXED((float)st.fontSize + st.padding.top + st.padding.bottom);

    float cursorPx = 0, cursorPy = 0;
    if (focused && state.font && f.cursor > 0) {
        size_t bytePos = Utf8ToBytes(buf, f.cursor);
        std::string_view textToCursor(buf.data(), bytePos);
        auto lastNl = textToCursor.rfind('\n');
        const char* lineStart = (lastNl == std::string_view::npos) ? textToCursor.data() : textToCursor.data() + lastNl + 1;
        size_t lineBytes = (lastNl == std::string_view::npos) ? textToCursor.size() : textToCursor.size() - lastNl - 1;

        int w = 0, h = 0;
        TTF_GetStringSize(state.font, lineStart, lineBytes, &w, &h);
        cursorPx = (float)w;

        int lineNum = 0;
        for (char c : textToCursor) if (c == '\n') ++lineNum;
        cursorPy = (float)(lineNum * lh);
    }

    int totalLines = 1;
    for (char c : buf) if (c == '\n') ++totalLines;

    if (cfg.multiline) {
        int displayLines = (cfg.maxHeight > 0) ? std::min(totalLines, (int)cfg.maxHeight) : totalLines;
        float h = (float)(displayLines * lh) + st.padding.top + st.padding.bottom;
        sizing.height = CLAY_SIZING_FIT();
        sizing.height.size.minMax.min = h;
        if (cfg.maxHeight > 0) {
            sizing.height.size.minMax.max = (float)((int)cfg.maxHeight * lh) + st.padding.top + st.padding.bottom;
        }
    }

    if (focused) {
        float contentW = (f.bounds.width > 0) ? f.bounds.width - st.padding.left - st.padding.right : 0;

        float contentH;
        if (!cfg.multiline) {
            contentH = (float)st.fontSize;
        } else if (cfg.maxHeight > 0) {
            contentH = (float)((int)cfg.maxHeight * lh);
        } else {
            contentH = (float)(totalLines * lh);
        }

        if (contentW > 0) {
            if (cursorPx - f.scrollX > contentW) f.scrollX = cursorPx - contentW;
            if (cursorPx - f.scrollX < 0)        f.scrollX = cursorPx;
            if (f.scrollX < 0)                    f.scrollX = 0;
        }
        if (cfg.multiline && contentH > 0) {
            float cursorBottom = cursorPy + (float)lh;
            if (cursorBottom - f.scrollY > contentH) f.scrollY = cursorBottom - contentH;
            if (cursorPy - f.scrollY < 0)            f.scrollY = cursorPy;

            float maxScroll = std::max(0.f, (float)(totalLines * lh) - contentH);
            f.scrollY = std::clamp(f.scrollY, 0.f, maxScroll);
        } else {
            f.scrollY = 0;
        }
    }

    bool clicked = false;

    CLAY({
        .id = id,
        .layout = {
            .sizing = sizing,
            .padding = st.padding,
            .childAlignment = {.y = cfg.multiline ? CLAY_ALIGN_Y_TOP : CLAY_ALIGN_Y_CENTER},
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
        },
        .backgroundColor = st.bgColor,
        .clip = {.horizontal = true, .vertical = cfg.multiline, .childOffset = {-f.scrollX, -f.scrollY}},
    }) {
        CLAY({
            .layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()}},
            .border = {.color = border, .width = {st.borderWidth, st.borderWidth, st.borderWidth, st.borderWidth}},
            .floating = {
                .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                .attachTo = CLAY_ATTACH_TO_PARENT,
            },
        }) {}

        if (Clay_Hovered() && state.mousePressed) {
            state.focusedId = id.id;
            state.focusConsumed = true;
            clicked = true;
            SDL_StartTextInput(events.target);
            f.cursor = hitTest(state.mouseX, state.mouseY);
            f.anchor = f.cursor;
        }

        if (empty) {
            CLAY_TEXT(Str(cfg.placeholder), CLAY_TEXT_CONFIG({
                .textColor = st.placeholderColor, .fontSize = st.fontSize,
                .wrapMode = CLAY_TEXT_WRAP_NONE,
            }));
        } else {
            CLAY_TEXT(Str(buf), CLAY_TEXT_CONFIG({
                .textColor = st.textColor, .fontSize = st.fontSize,
                .wrapMode = cfg.multiline ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE,
            }));

            if (focused) {
                float ox = (float)st.padding.left - f.scrollX;
                float centerY = cfg.multiline ? 0.f : ((float)st.fontSize - (float)lh) / 2.f;
                float oy = (float)st.padding.top + centerY - f.scrollY;

                if (f.hasSelection()) {
                    size_t lo = f.selectionStart(), hi = f.selectionEnd();
                    size_t cpOff = 0, pos = 0;
                    int line = 0;
                    while (true) {
                        size_t nl = buf.find('\n', pos);
                        size_t lineEnd = (nl == std::string::npos) ? buf.size() : nl;
                        const char* lineData = buf.c_str() + pos;
                        size_t lineBytes = lineEnd - pos;
                        size_t lineCP = Utf8Len(lineData, lineBytes);
                        size_t cpLineEnd = cpOff + lineCP;

                        if (lo < cpLineEnd + 1 && hi > cpOff) {
                            size_t selS = std::max(lo, cpOff);
                            size_t selE = std::min(hi, cpLineEnd);
                            float xStart = 0;
                            if (selS > cpOff) {
                                int w = 0, h = 0;
                                TTF_GetStringSize(state.font, lineData, Utf8ToBytes(lineData, lineBytes, selS - cpOff), &w, &h);
                                xStart = (float)w;
                            }
                            int w = 0, h = 0;
                            TTF_GetStringSize(state.font, lineData, Utf8ToBytes(lineData, lineBytes, selE - cpOff), &w, &h);
                            float selW = std::max((float)w - xStart, (float)st.fontSize / 2);

                            CLAY({
                                .layout = {.sizing = {CLAY_SIZING_FIXED(selW), CLAY_SIZING_FIXED((float)lh)}},
                                .backgroundColor = {70, 130, 255, 128},
                                .floating = {
                                    .offset = {ox + xStart, oy + (float)(line * lh)},
                                    .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP},
                                    .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                                    .attachTo = CLAY_ATTACH_TO_PARENT,
                                    .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT,
                                },
                            }) {}
                        }

                        if (nl == std::string::npos) break;
                        cpOff += lineCP + 1;
                        pos = nl + 1;
                        ++line;
                    }
                } else {
                    CLAY({
                        .layout = {.sizing = {CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED((float)lh)}},
                        .backgroundColor = st.textColor,
                        .floating = {
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

    if constexpr (kNumeric) return committed;
    else return clicked;
}

template bool Interface::input<std::string>(InterfaceState&, const WindowEvents&, Clay_ElementId, std::string&, InputConfig, InputStyle);
template bool Interface::input<int>(InterfaceState&, const WindowEvents&, Clay_ElementId, int&, InputConfig, InputStyle);
template bool Interface::input<uint16_t>(InterfaceState&, const WindowEvents&, Clay_ElementId, uint16_t&, InputConfig, InputStyle);
template bool Interface::input<float>(InterfaceState&, const WindowEvents&, Clay_ElementId, float&, InputConfig, InputStyle);
template bool Interface::input<double>(InterfaceState&, const WindowEvents&, Clay_ElementId, double&, InputConfig, InputStyle);
