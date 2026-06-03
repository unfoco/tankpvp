#include "interface.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "util/time.h"

static inline auto Utf8CpBytes(unsigned char lead) noexcept -> size_t {
    if (lead < 0x80) {
        return 1;
    }
    if (lead < 0xE0) {
        return 2;
    }
    if (lead < 0xF0) {
        return 3;
    }
    return 4;
}

static inline auto Utf8Decode(const char* p, size_t len) noexcept -> uint32_t {
    auto b = [&](size_t i) -> uint32_t { return static_cast<unsigned char>(p[i]); };
    if (len == 2) {
        return ((b(0) & 0x1F) << 6) | (b(1) & 0x3F);
    }
    if (len == 3) {
        return ((b(0) & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F);
    }
    if (len == 4) {
        return ((b(0) & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);
    }
    return b(0);
}

static inline auto Utf8Len(const char* s, size_t bytes) noexcept -> size_t {
    size_t n = 0;
    for (size_t i = 0; i < bytes; ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            ++n;
        }
    }
    return n;
}

static inline auto Utf8Len(const std::string& s) noexcept -> size_t {
    return Utf8Len(s.data(), s.size());
}

static inline auto Utf8ToBytes(const char* s, size_t bytes, size_t cp) noexcept -> size_t {
    size_t off = 0;
    for (size_t i = 0; i < cp && off < bytes; ++i) {
        off += Utf8CpBytes(static_cast<unsigned char>(s[off]));
    }
    return off;
}

static inline auto Utf8ToBytes(const std::string& s, size_t cp) noexcept -> size_t {
    return Utf8ToBytes(s.data(), s.size(), cp);
}

static inline void Utf8Erase(std::string& s, size_t cpStart, size_t cpEnd) {
    size_t a = Utf8ToBytes(s, cpStart);
    size_t b = Utf8ToBytes(s, cpEnd);
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
    world.component<InterfaceTransition>().add(flecs::Singleton);

    TTF_Init();
    auto* font = TTF_OpenFont("asset/font.ttf", 16);
    if (font == nullptr) {
        SDL_Log("Failed to load font: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    TTF_SetFontHinting(font, TTF_HINTING_MONO);

    world.set<InterfaceState>({.font = font});
    world.set<InterfaceCommands>({});
    world.set(InterfacePage::Main);
    world.set<InterfacePrevious>({.page = InterfacePage::Main});
    world.set<ChatLog>({});
    world.set<ChatInput>({});
    world.set<InputCapture>({});
    world.set<InterfaceTransition>({});
    world.set<ServerList>({.entries = {{.name = "Localhost", .address = "127.0.0.1", .port = 5000}}});

    world.system<InterfaceState>("interface::frame").kind(flecs::PreFrame).each(Interface::frame);
    world.system<InterfaceState, WindowEvents>("interface::event").kind(flecs::PreUpdate).each(Interface::event);
    world.system<InterfaceState, InterfaceCommands, InterfacePage, InterfacePrevious, WindowEvents>("interface::build").kind(flecs::OnUpdate).each(Interface::build);
}

void Interface::frame(flecs::iter&, size_t, InterfaceState& state) {
    state.prevFocusedId = state.focusedId;
    state.mousePressed = false;
    state.focusConsumed = false;
    state.textPool.clear();
}

void Interface::event(flecs::iter&, size_t, InterfaceState& state, const WindowEvents& events) {
    for (const auto& e : events) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            state.mousePressed = true;
            state.mouseDown = true;
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            state.mouseDown = false;
            state.activeId = 0;
        } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
            state.mouseX = e.motion.x;
            state.mouseY = e.motion.y;
        }
    }
}

static auto opposite_dir(TransitionDir d) -> TransitionDir {
    switch (d) {
        case TransitionDir::Left:
            return TransitionDir::Right;
        case TransitionDir::Right:
            return TransitionDir::Left;
        case TransitionDir::Up:
            return TransitionDir::Down;
        case TransitionDir::Down:
            return TransitionDir::Up;
    }
    return d;
}

static void pick_transition(InterfacePage from, InterfacePage to, TransitionKind& kind, TransitionDir& dir, double& duration) {
    (void)from;
    switch (to) {
        case InterfacePage::Host:
        case InterfacePage::Connect:
        case InterfacePage::Server:
        case InterfacePage::Settings:
            kind = TransitionKind::Slide;
            dir = TransitionDir::Left;
            duration = 0.3;
            break;
        case InterfacePage::Main:
            kind = TransitionKind::Slide;
            dir = TransitionDir::Right;
            duration = 0.3;
            break;
        default:
            kind = TransitionKind::Crossfade;
            dir = TransitionDir::Left;
            duration = 0.1;
            break;
    }
}

void Interface::build(flecs::iter& it, size_t i, InterfaceState& state, InterfaceCommands& cmds, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) {
    InterfacePage shown = page;
    if (auto& tr = it.world().get_mut<InterfaceTransition>(); shown != tr.shown) {
        InterfacePage from = tr.shown;
        if (from == tr.lastTo && shown == tr.lastFrom) {
            tr.dir = opposite_dir(tr.dir);
        } else {
            pick_transition(from, shown, tr.kind, tr.dir, tr.duration);
        }
        tr.lastFrom = from;
        tr.lastTo = shown;
        tr.shown = shown;
        tr.start = util::now();
    }

    switch (page) {
        case InterfacePage::Main:
            cmds.list = Interface::main(it, state, page, prev, events);
            break;
        case InterfacePage::Host:
            cmds.list = Interface::host(it, state, page, prev, events);
            break;
        case InterfacePage::Pause:
            cmds.list = Interface::pause(it, state, page, prev, events);
            break;
        case InterfacePage::Ingame:
            cmds.list = Interface::ingame(it, state, page, prev, events);
            break;
        case InterfacePage::Server:
            cmds.list = Interface::server(it, state, page, prev, events);
            break;
        case InterfacePage::Connect:
            cmds.list = Interface::connect(it, state, page, prev, events);
            break;
        case InterfacePage::Settings:
            cmds.list = Interface::settings(it, state, page, prev, events);
            break;
        case InterfacePage::Chat:
            cmds.list = Interface::chat(it, state, page, prev, events);
            break;
        case InterfacePage::Status:
            cmds.list = Interface::status(it, state, page, prev, events);
            break;
        case InterfacePage::None:
            cmds.list = {};
            break;
    }

    for (int32_t i = 0; i < cmds.list.length; ++i) {
        auto* cmd = Clay_RenderCommandArray_Get(&cmds.list, i);
        if (auto* f = state.findField(cmd->id)) {
            f->bounds = cmd->boundingBox;
        }
    }

    if (state.mousePressed && !state.focusConsumed && state.focusedId != 0) {
        state.focusedId = 0;
        SDL_StopTextInput(events.target);
    }

    it.world().get_mut<InputCapture>().active = (state.focusedId != 0);
}

auto Interface::button(InterfaceState& state, Clay_ElementId id, const char* label, ButtonStyle st) -> bool {
    bool clicked = false;
    Clay_Sizing sizing = {};
    if (st.width > 0) {
        sizing.width = CLAY_SIZING_FIXED(st.width);
    }
    CLAY({
        .id = id,
        .layout = {.sizing = sizing, .padding = st.padding, .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = st.color,
        .cornerRadius = CLAY_CORNER_RADIUS(st.cornerRadius),
    }) {
        if (Clay_Hovered() && state.mousePressed) {
            clicked = true;
        }
        CLAY_TEXT(Str(label), CLAY_TEXT_CONFIG({
                                  .textColor = st.textColor,
                                  .fontSize = st.fontSize,
                                  .wrapMode = CLAY_TEXT_WRAP_NONE,
                              }));
    }
    return clicked;
}

auto Interface::wrap(InterfaceState& state, const std::string& text, uint16_t fontSize, float maxWidth) -> std::string {
    if (state.font == nullptr || maxWidth <= 0) {
        return text;
    }
    TTF_SetFontSize(state.font, fontSize);

    auto measure = [&](const std::string& s) -> float {
        if (s.empty()) {
            return 0;
        }
        int w = 0;
        int h = 0;
        TTF_GetStringSize(state.font, s.c_str(), s.size(), &w, &h);
        return static_cast<float>(w);
    };

    std::string out;
    std::string line;
    size_t i = 0;
    size_t n = text.size();

    while (i < n) {
        if (text[i] == '\n') {
            out += line;
            out += '\n';
            line.clear();
            ++i;
            continue;
        }

        if (text[i] == ' ') {
            if (!line.empty()) {
                if (measure(line + " ") <= maxWidth) {
                    line += ' ';
                } else {
                    out += line;
                    out += '\n';
                    line.clear();
                }
            }
            ++i;
            continue;
        }

        size_t j = i;
        while (j < n && text[j] != ' ' && text[j] != '\n') {
            j += Utf8CpBytes(static_cast<unsigned char>(text[j]));
        }
        std::string word = text.substr(i, j - i);

        if (measure(line + word) <= maxWidth) {
            line += word;
            i = j;
        } else if (measure(word) <= maxWidth && !line.empty() && measure(line) >= maxWidth * 0.5F) {
            out += line;
            out += '\n';
            line.clear();
            line = word;
            i = j;
        } else {
            size_t k = i;
            while (k < j) {
                size_t cb = Utf8CpBytes(static_cast<unsigned char>(text[k]));
                std::string cp = text.substr(k, cb);
                if (line.empty() || measure(line + cp) <= maxWidth) {
                    line += cp;
                    k += cb;
                } else {
                    out += line;
                    out += '\n';
                    line.clear();
                }
            }
            i = j;
        }
    }

    out += line;
    return out;
}

auto Interface::toggle(InterfaceState& state, Clay_ElementId id, bool& value, ToggleStyle st) -> bool {
    bool toggled = false;
    float knob = st.height - (st.knobPad * 2);
    auto pad = static_cast<uint16_t>(st.knobPad);

    CLAY({
        .id = id,
        .layout =
            {
                .sizing = {CLAY_SIZING_FIXED(st.width), CLAY_SIZING_FIXED(st.height)},
                .padding = {pad, pad, pad, pad},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
        .backgroundColor = value ? st.onColor : st.offColor,
        .cornerRadius = CLAY_CORNER_RADIUS(st.height / 2),
    }) {
        if (Clay_Hovered() && state.mousePressed) {
            value = !value;
            toggled = true;
        }
        if (value) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW()}}}) {}
        }
        CLAY({
            .layout = {.sizing = {CLAY_SIZING_FIXED(knob), CLAY_SIZING_FIXED(knob)}},
            .backgroundColor = st.knobColor,
            .cornerRadius = CLAY_CORNER_RADIUS(knob / 2),
        }) {}
    }
    return toggled;
}

auto Interface::slider(InterfaceState& state, Clay_ElementId id, float& value, float lo, float hi, SliderStyle st) -> bool {
    float prev = value;
    float range = hi - lo;
    if (range <= 0) {
        return false;
    }

    if (state.activeId == id.id && state.mouseDown) {
        float delta = (state.mouseX - state.dragOriginX) / st.trackWidth * range;
        value = std::clamp(state.dragOriginValue + delta, lo, hi);
    }

    float norm = std::clamp((value - lo) / range, 0.F, 1.F);
    float fill = norm * st.trackWidth;

    CLAY({
        .id = id,
        .layout =
            {
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
            .layout =
                {
                    .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIXED(st.trackHeight)},
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                },
            .backgroundColor = st.trackColor,
            .cornerRadius = CLAY_CORNER_RADIUS(st.trackHeight / 2),
        }) {
            if (fill > 0.5F) {
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

template <typename T>
auto Interface::input(InterfaceState& state, const WindowEvents& events, Clay_ElementId id, T& value, InputConfig cfg, InputStyle st) -> bool {
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

    size_t len = Utf8Len(buf);
    f.cursor = std::min(f.cursor, len);
    f.anchor = std::min(f.anchor, len);

    auto eraseSelection = [&]() -> bool {
        if (!f.hasSelection()) {
            return false;
        }
        Utf8Erase(buf, f.selectionStart(), f.selectionEnd());
        f.cursor = f.selectionStart();
        f.collapseSelection();
        return true;
    };

    auto copySelection = [&]() -> void {
        if (!f.hasSelection()) {
            return;
        }
        size_t a = Utf8ToBytes(buf, f.selectionStart());
        size_t b = Utf8ToBytes(buf, f.selectionEnd());
        std::string clip = buf.substr(a, b - a);
        SDL_SetClipboardText(clip.c_str());
    };

    auto insertFiltered = [&](const char* text) -> void {
        eraseSelection();
        for (const char* p = text; *p;) {
            size_t cpLen = Utf8CpBytes(static_cast<unsigned char>(*p));
            if (cfg.maxLength && Utf8Len(buf) >= cfg.maxLength) {
                break;
            }

            bool ok = true;
            if (cfg.allow) {
                for (size_t i = 0; i < cpLen && p[i]; ++i) {
                    if (!cfg.allow(p[i])) {
                        ok = false;
                        break;
                    }
                }
            }

            if (ok) {
                uint32_t cp = Utf8Decode(p, cpLen);
                bool regular = cp == ' ' || cp == '\n' || (cp >= 0x21 && cp <= 0x7E) || (cp >= 0xC0 && cp <= 0x24F);
                if (!regular || (state.font && !TTF_FontHasGlyph(state.font, cp))) {
                    ok = false;
                }
            }

            if (ok) {
                Utf8Insert(buf, f.cursor, p, cpLen);
                ++f.cursor;
            }
            p += cpLen;
        }
        f.collapseSelection();
    };

    auto measureLine = [&]() -> int {
        if (!state.font) {
            return static_cast<int>(st.fontSize);
        }
        TTF_SetFontSize(state.font, st.fontSize);
        int w = 0;
        int h = 0;
        TTF_GetStringSize(state.font, "A", 1, &w, &h);
        return h > 0 ? h : static_cast<int>(st.fontSize);
    };

    auto hitTest = [&](float mx, float my) -> size_t {
        if (!state.font || buf.empty()) {
            return 0;
        }
        TTF_SetFontSize(state.font, st.fontSize);

        int lh = measureLine();
        float relX = std::max(0.F, mx - f.bounds.x - static_cast<float>(st.padding.left) + f.scrollX);
        float relY = std::max(0.F, my - f.bounds.y - static_cast<float>(st.padding.top) + f.scrollY);
        int targetLine = static_cast<int>(relY / static_cast<float>(lh));

        size_t cpOff = 0;
        size_t pos = 0;
        int line = 0;
        while (true) {
            size_t nl = buf.find('\n', pos);
            size_t lineEnd = (nl == std::string::npos) ? buf.size() : nl;
            const char* lineData = buf.c_str() + pos;
            size_t lineBytes = lineEnd - pos;
            size_t lineCP = Utf8Len(lineData, lineBytes);

            if (line == targetLine || nl == std::string::npos) {
                if (relX <= 0) {
                    return cpOff;
                }
                for (size_t i = 1; i <= lineCP; ++i) {
                    int w = 0;
                    int h = 0;
                    TTF_GetStringSize(state.font, lineData, Utf8ToBytes(lineData, lineBytes, i), &w, &h);
                    if (static_cast<float>(w) >= relX) {
                        int pw = 0;
                        TTF_GetStringSize(state.font, lineData, Utf8ToBytes(lineData, lineBytes, i - 1), &pw, &h);
                        return cpOff + ((static_cast<float>(w) - relX < relX - static_cast<float>(pw)) ? i : i - 1);
                    }
                }
                return cpOff + lineCP;
            }

            cpOff += lineCP + 1;
            pos = nl + 1;
            ++line;
        }
    };

    if (cfg.disabled) {
        st.textColor.a *= 0.4F;
        st.bgColor.r *= 0.6F;
        st.bgColor.g *= 0.6F;
        st.bgColor.b *= 0.6F;
    }

    bool focused = (state.focusedId == id.id) && !cfg.disabled;
    bool focusGain = focused && (state.prevFocusedId != id.id);
    bool focusLose = !focused && (state.prevFocusedId == id.id);

    if (focusGain) {
        if constexpr (kNumeric) {
            buf = formatValue(value);
        }
        f.cursor = Utf8Len(buf);
        f.anchor = f.cursor;
        f.blinkBase = util::now();
    }

    bool enterCommit = false;

    if (focused) {
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
                    --f.cursor;
                }
                if (!shift) {
                    f.collapseSelection();
                }
                continue;
            }
            if (key == SDLK_RIGHT) {
                size_t l = Utf8Len(buf);
                if (mod) {
                    f.cursor = l;
                } else if (!shift && f.hasSelection()) {
                    f.cursor = f.selectionEnd();
                } else if (f.cursor < l) {
                    ++f.cursor;
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
                f.cursor = Utf8Len(buf);
                if (!shift) {
                    f.collapseSelection();
                }
                continue;
            }

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

            if (mod && key == SDLK_A) {
                f.anchor = 0;
                f.cursor = Utf8Len(buf);
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
                if constexpr (kNumeric) {
                    buf = formatValue(value);
                }
                state.focusedId = 0;
                SDL_StopTextInput(events.target);
                continue;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if (!kNumeric && cfg.multiline) {
                    int lines = 1;
                    for (char c : buf) {
                        if (c == '\n') {
                            ++lines;
                        }
                    }
                    if (!cfg.maxLine || std::cmp_less(lines, cfg.maxLine)) {
                        eraseSelection();
                        Utf8Insert(buf, f.cursor, "\n", 1);
                        ++f.cursor;
                        f.collapseSelection();
                    }
                } else if (kNumeric || cfg.commitOnEnter) {
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
    }

    bool committed = false;
    if constexpr (kNumeric) {
        if (focusLose || enterCommit) {
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
        f.cursor = hitTest(state.mouseX, state.mouseY);
    }

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
        size_t bytePos = Utf8ToBytes(buf, f.cursor);
        std::string_view textToCursor(buf.data(), bytePos);
        auto lastNl = textToCursor.rfind('\n');
        const char* lineStart = (lastNl == std::string_view::npos) ? textToCursor.data() : textToCursor.data() + lastNl + 1;
        size_t lineBytes = (lastNl == std::string_view::npos) ? textToCursor.size() : textToCursor.size() - lastNl - 1;

        int w = 0;
        int h = 0;
        TTF_GetStringSize(state.font, lineStart, lineBytes, &w, &h);
        cursorPx = static_cast<float>(w);

        int lineNum = 0;
        for (char c : textToCursor) {
            if (c == '\n') {
                ++lineNum;
            }
        }
        cursorPy = static_cast<float>(lineNum * lh);
    }

    int totalLines = 1;
    for (char c : buf) {
        if (c == '\n') {
            ++totalLines;
        }
    }

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

        if (Clay_Hovered() && state.mousePressed && !cfg.disabled) {
            state.focusedId = id.id;
            state.focusConsumed = true;
            clicked = true;
            SDL_StartTextInput(events.target);
            f.cursor = hitTest(state.mouseX, state.mouseY);
            f.anchor = f.cursor;
            f.blinkBase = util::now();
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
            if (cfg.multiline) {
                float ww = (f.bounds.width > 0) ? f.bounds.width - static_cast<float>(st.padding.left) - static_cast<float>(st.padding.right) : 0;
                const std::string& disp = state.intern(Interface::wrap(state, buf, st.fontSize, ww));
                CLAY_TEXT(Str(disp), CLAY_TEXT_CONFIG({
                                         .textColor = st.textColor,
                                         .fontSize = st.fontSize,
                                         .wrapMode = CLAY_TEXT_WRAP_NEWLINES,
                                     }));
            } else {
                CLAY_TEXT(Str(buf), CLAY_TEXT_CONFIG({
                                        .textColor = st.textColor,
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
                    size_t cpOff = 0;
                    size_t pos = 0;
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
                                int w = 0;
                                int h = 0;
                                TTF_GetStringSize(state.font, lineData, Utf8ToBytes(lineData, lineBytes, selS - cpOff), &w, &h);
                                xStart = static_cast<float>(w);
                            }
                            int w = 0;
                            int h = 0;
                            TTF_GetStringSize(state.font, lineData, Utf8ToBytes(lineData, lineBytes, selE - cpOff), &w, &h);
                            float selW = std::max(static_cast<float>(w) - xStart, static_cast<float>(st.fontSize) / 2);

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
                        cpOff += lineCP + 1;
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

    if constexpr (kNumeric) {
        return committed;
    } else {
        return clicked;
    }
}

template bool Interface::input<std::string>(InterfaceState&, const WindowEvents&, Clay_ElementId, std::string&, InputConfig, InputStyle);
template bool Interface::input<int>(InterfaceState&, const WindowEvents&, Clay_ElementId, int&, InputConfig, InputStyle);
template bool Interface::input<uint16_t>(InterfaceState&, const WindowEvents&, Clay_ElementId, uint16_t&, InputConfig, InputStyle);
template bool Interface::input<float>(InterfaceState&, const WindowEvents&, Clay_ElementId, float&, InputConfig, InputStyle);
template bool Interface::input<double>(InterfaceState&, const WindowEvents&, Clay_ElementId, double&, InputConfig, InputStyle);
