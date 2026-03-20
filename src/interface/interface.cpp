#include <algorithm>

#include "interface.h"

#include "component/interface.h"

inline void PopUtf8(std::string& s) {
    if (s.empty()) return;
    while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
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
    state.mousePressed = false;
}

void Interface::event(flecs::iter&, size_t, InterfaceState& state, const WindowEvents& events) {
    for (const auto& event : events) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            state.mousePressed = true;
            state.mouseDown = true;
            state.focusedId = 0;
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
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
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
            if (fill > 0.5f) {
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

bool Interface::input(InterfaceState& state, const WindowEvents& events, Clay_ElementId id, std::string& value, const char* placeholder, InputStyle s) {
    if (state.focusedId == id.id) {
        for (const auto& event : events) {
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                value += event.text.text;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
                if ((event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) && !event.key.repeat) {
                    value += '\n';
                } else if (event.key.key == SDLK_TAB && !event.key.repeat) {
                    value += '\t';
                } else if (event.key.key == SDLK_BACKSPACE) {
                    PopUtf8(value);
                } else if (ctrl && event.key.key == SDLK_V && !event.key.repeat) {
                    const char* cb = SDL_GetClipboardText();
                    if (cb && cb[0]) value += cb;
                }
            }
        }
    }

    bool clicked = false;
    bool empty = value.empty();
    Clay_Color border = (state.focusedId == id.id) ? s.focusBorderColor : s.borderColor;

    CLAY({
        .id = id,
        .layout = {
            .sizing = s.sizing,
            .padding = s.padding,
            .layoutDirection = CLAY_LEFT_TO_RIGHT
        },
        .backgroundColor = s.backgroundColor,
        .border = {
            .color = border,
            .width = {
                .left = s.borderWidth,
                .right = s.borderWidth,
                .top = s.borderWidth,
                .bottom = s.borderWidth
            }
        },
    }) {
        if (Clay_Hovered() && state.mousePressed) {
            state.focusedId = id.id; clicked = true;
        }
        CLAY_TEXT(empty ? Str(placeholder) : Str(value),
            CLAY_TEXT_CONFIG({
                .textColor = empty ? s.placeholderColor : s.textColor,
                .fontSize = s.fontSize,
            })
        );
    }
    return clicked;
}
