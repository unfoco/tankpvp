#include <algorithm>
#include <regex>
#include <string>

#include "interface.h"

inline void Utf8Pop(std::string& s) {
    if (s.empty()) return;
    while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

inline size_t Utf8Len(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) n++;
    }
    return n;
}

Interface::Interface(flecs::world& world) {
    world.component<InterfaceState>()
        .add(flecs::Singleton);
    world.component<InterfacePage>()
        .add(flecs::Singleton);
    world.component<InterfacePrevious>()
        .add(flecs::Singleton);
    world.component<InterfaceCommands>()
        .add(flecs::Singleton);

    world.set<InterfaceState>({});
    world.set<InterfaceCommands>({});

    world.set(InterfacePage::Main);
    world.set<InterfacePrevious>({
        .page = InterfacePage::Main,
    });

    world.system<InterfaceState>("interface::frame")
        .kind(flecs::PreFrame)
        .each(Interface::frame);

    world.system<InterfaceState, WindowEvents>("interface::event")
        .kind(flecs::PreUpdate)
        .each(Interface::event);

    world.system<InterfaceState, InterfaceCommands, InterfacePage, InterfacePrevious, WindowEvents>("interface::build")
        .kind(flecs::OnUpdate)
        .each(Interface::build);
}

void Interface::frame(flecs::iter&, size_t, InterfaceState& state) {
    state.focusedLastId = state.focusedId;
    state.mousePressed = false;
}

void Interface::event(flecs::iter&, size_t, InterfaceState& state, const WindowEvents& events) {
    for (const auto& event : events) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            state.mousePressed = true;
            state.mouseDown = true;
            state.focusedId = 0;
            SDL_StopTextInput(events.target);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            state.mouseDown = false;
            state.activeId = 0;
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            state.mouseX = event.motion.x;
            state.mouseY = event.motion.y;
        }
    }
}

void Interface::build(flecs::iter& it, size_t, InterfaceState& state, InterfaceCommands& commands, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) {
    switch (page) {
    case InterfacePage::Main:
        commands.list = Interface::main(it, state, page, prev, events);
        break;
    case InterfacePage::Pause:
        commands.list = Interface::pause(it, state, page, prev, events);
        break;
    case InterfacePage::Ingame:
        commands.list = Interface::ingame(it, state, page, prev, events);
        break;
    case InterfacePage::Connect:
        commands.list = Interface::connect(it, state, page, prev, events);
        break;
    case InterfacePage::Settings:
        commands.list = Interface::settings(it, state, page, prev, events);
        break;
    case InterfacePage::None:
        commands.list = {};
    }
}

bool Interface::button(InterfaceState& state, Clay_ElementId id, const char* label, ButtonStyle s) {
    bool clicked = false;
    CLAY({
        .id = id,
        .layout = { .padding = s.padding },
        .backgroundColor = s.color,
        .cornerRadius = CLAY_CORNER_RADIUS(s.cornerRadius),
    }) {
        if (Clay_Hovered() && state.mousePressed) clicked = true;
        CLAY_TEXT(Str(label), CLAY_TEXT_CONFIG({
            .textColor = s.textColor, .fontSize = s.fontSize,
        }));
    }
    return clicked;
}

bool Interface::toggle(InterfaceState& state, Clay_ElementId id, bool& value, ToggleStyle s) {
    bool toggled = false;
    float knob = s.height - s.knobPad * 2;
    uint16_t p = (uint16_t)s.knobPad;
    CLAY({
        .id = id,
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(s.width), .height = CLAY_SIZING_FIXED(s.height) },
            .padding = {p, p, p, p},
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
        },
        .backgroundColor = value ? s.onColor : s.offColor,
        .cornerRadius = CLAY_CORNER_RADIUS(s.height / 2),
    }) {
        if (Clay_Hovered() && state.mousePressed) { value = !value; toggled = true; }
        if (value) { CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW() } } }) {} }
        CLAY({
            .layout = { .sizing = { CLAY_SIZING_FIXED(knob), CLAY_SIZING_FIXED(knob) } },
            .backgroundColor = s.knobColor,
            .cornerRadius = CLAY_CORNER_RADIUS(knob / 2),
        }) {}
    }
    return toggled;
}

bool Interface::slider(InterfaceState& state, Clay_ElementId id, float& value, float low, float high, SliderStyle s) {
    float old = value, range = high - low;
    if (range <= 0) return false;
    if (state.activeId == id.id && state.mouseDown) {
        float d = (state.mouseX - state.dragStartX) / s.trackWidth * range;
        value = std::clamp(state.dragStartVal + d, low, high);
    }
    float norm = std::clamp((value - low) / range, 0.f, 1.f);
    float fill = norm * s.trackWidth;

    CLAY({
        .id = id,
        .layout = {
            .sizing = { CLAY_SIZING_FIXED(s.trackWidth), CLAY_SIZING_FIXED(s.trackHeight + 14) },
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
        },
    }) {
        if (Clay_Hovered() && state.mousePressed) {
            state.activeId = id.id; state.dragStartX = state.mouseX; state.dragStartVal = value;
        }
        CLAY({
            .layout = {
                .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_FIXED(s.trackHeight) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
            .backgroundColor = s.trackColor,
            .cornerRadius = CLAY_CORNER_RADIUS(s.trackHeight / 2),
        }) {
            if (fill> 0.5f) {
                CLAY({
                    .layout = { .sizing = { CLAY_SIZING_FIXED(fill), CLAY_SIZING_GROW() } },
                    .backgroundColor = s.fillColor,
                    .cornerRadius = CLAY_CORNER_RADIUS(s.trackHeight / 2),
                }) {}
            }
        }
    }
    return value != old;
}

template<typename T>
bool Interface::input(InterfaceState& state, const WindowEvents& events, Clay_ElementId id, T& value, InputConfig cfg, InputStyle s) {
    constexpr bool numeric = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;
    constexpr bool stringy = std::is_same_v<T, std::string>;
    static_assert(numeric || stringy, "T must be std::string or a numeric type");

    const char* fmt = cfg.format;
    if constexpr (std::is_floating_point_v<T>) {
        if (!fmt) fmt = "%.2f";
        if (!cfg.allow) cfg.allow = InputFilter::Float;
    } else if constexpr (std::is_integral_v<T>) {
        if (!fmt) fmt = "%d";
        if (!cfg.allow) cfg.allow = InputFilter::Signed;
    }

    auto format = [&]<typename U>(U v) -> std::string {
        if constexpr (!numeric) { return {}; }
        char tmp[64];
        if constexpr (std::is_floating_point_v<U>) {
            snprintf(tmp, sizeof(tmp), fmt, (double)v);
        } else {
            snprintf(tmp, sizeof(tmp), fmt, (long long)v);
        }
        return tmp;
    };
    auto append = [&](std::string& buf, char c) {
        if (cfg.len && Utf8Len(buf) >= cfg.len) return;
        if (cfg.allow && !cfg.allow(c)) return;
        buf += c;
    };
    std::string& buf = [&]() -> std::string& {
        if constexpr (stringy) return value;
        else return state.editBuffers[id.id];
    }();

    bool focused   = (state.focusedId == id.id);
    bool focusGain = focused && (state.focusedLastId != id.id);
    bool focusLose = !focused && (state.focusedLastId == id.id);
    if constexpr (numeric) {
        if (focusGain) {
            buf = format(value);
        }
    }

    bool enterCommit = false;
    if (focused) {
        for (const auto& event : events) {
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                const char* t = event.text.text;
                size_t tlen = strlen(t);

                if (cfg.len && Utf8Len(buf) >= cfg.len) continue;

                bool ok = true;
                for (size_t i = 0; i < tlen; ++i) {
                    if (cfg.allow && !cfg.allow(t[i])) {
                        ok = false;
                        break;
                    }
                }

                if (ok) buf.append(t, tlen);
                continue;
            }

            if (event.type != SDL_EVENT_KEY_DOWN) continue;
            if (event.key.repeat && event.key.key != SDLK_BACKSPACE) continue;

            bool mod = (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
            auto key = event.key.key;
            if (key == SDLK_BACKSPACE) {
                Utf8Pop(buf);
                continue;
            }
            if (key == SDLK_ESCAPE) {
                if constexpr (numeric) { buf = format(value); }
                state.focusedId = 0;
                SDL_StopTextInput(events.target);
                continue;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                if constexpr (numeric) {
                    enterCommit = true;
                } else if (cfg.multiline) {
                    buf += '\n';
                } else if (cfg.commitOnEnter) {
                    enterCommit = true;
                }
                continue;
            }
            if constexpr (stringy) {
                if (key == SDLK_TAB && cfg.multiline) {
                    buf += '\t';
                    continue;
                }
            }
            if (mod && key == SDLK_V) {
                const char* cb = SDL_GetClipboardText();
                if (!cb) continue;

                const char* p = cb;
                while (*p) {
                    size_t cplen = 1;
                    unsigned char lead = (unsigned char)*p;

                    if (lead >= 0xF0) cplen = 4;
                    else if (lead >= 0xE0) cplen = 3;
                    else if (lead >= 0xC0) cplen = 2;
                    if (cfg.len && Utf8Len(buf) >= cfg.len) break;

                    bool ok = true;
                    for (size_t i = 0; i < cplen && p[i]; ++i) {
                        if (cfg.allow && !cfg.allow(p[i])) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) buf.append(p, cplen);
                    p += cplen;
                }
                continue;
            }
        }
        if (enterCommit) {
            state.focusedId = 0;
            SDL_StopTextInput(events.target);
        }
    }

    bool committed = false;
    if constexpr (numeric) {
        if (focusLose || enterCommit) {
            char* end = nullptr;
            if constexpr (std::is_floating_point_v<T>) {
                double d = strtod(buf.c_str(), &end);
                if (end != buf.c_str()) { value = std::clamp(static_cast<T>(d), static_cast<T>(cfg.min), static_cast<T>(cfg.max)); }
            } else {
                long long ll = strtoll(buf.c_str(), &end, 10);
                if (end != buf.c_str()) { value = std::clamp(static_cast<T>(ll), static_cast<T>(cfg.min), static_cast<T>(cfg.max)); }
            }
            buf = format(value);
            if (value == T{} && cfg.placeholder[0]) {
                buf.clear();
            }
            committed = true;
        }
        if (!focused && !focusLose) {
            if (value == T{} && cfg.placeholder[0]) {
                buf.clear();
            } else {
                buf = format(value);
            }
        }
    } else {
        if ((focusLose || enterCommit) && cfg.validate) {
            bool valid = true;
            if (cfg.pattern) {
                valid = std::regex_match(value, std::regex(cfg.pattern));
            }
            if (valid && cfg.validate) {
                valid = cfg.validate(value);
            }
            committed = valid;
        }
    }

    bool clicked = false;
    bool empty = buf.empty();
    Clay_Color border = focused ? s.focusBorderColor : s.borderColor;
    CLAY({
        .id = id,
        .layout = { .sizing = s.sizing, .padding = s.padding, .layoutDirection = CLAY_LEFT_TO_RIGHT },
        .backgroundColor = s.backgroundColor,
        .border = { .color = border, .width = { s.borderWidth, s.borderWidth, s.borderWidth, s.borderWidth } },
    }) {
        if (Clay_Hovered() && state.mousePressed) {
            state.focusedId = id.id;
            clicked = true;
            SDL_StartTextInput(events.target);
        }
        CLAY_TEXT(
            empty ? Str(cfg.placeholder) : Str(buf),
            CLAY_TEXT_CONFIG({ .textColor = empty ? s.placeholderColor : s.textColor, .fontSize = s.fontSize })
        );
    }

    if constexpr (numeric) return committed;
    else return clicked;
}

template bool Interface::input<std::string>(InterfaceState&, const WindowEvents&, Clay_ElementId, std::string&, InputConfig, InputStyle);
template bool Interface::input<int>(InterfaceState&, const WindowEvents&, Clay_ElementId, int&, InputConfig, InputStyle);
template bool Interface::input<uint16_t>(InterfaceState&, const WindowEvents&, Clay_ElementId, uint16_t&, InputConfig, InputStyle);
template bool Interface::input<float>(InterfaceState&, const WindowEvents&, Clay_ElementId, float&, InputConfig, InputStyle);
template bool Interface::input<double>(InterfaceState&, const WindowEvents&, Clay_ElementId, double&, InputConfig, InputStyle);
