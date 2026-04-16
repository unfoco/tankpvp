#include "input.h"

#include <clay.h>

#include "component/object.h"

Input::Input(flecs::world& world) {
    world.component<InputFlags>()
        .bit("Left", InputFlags::Left)
        .bit("Right", InputFlags::Right)
        .bit("Backward", InputFlags::Backward)
        .bit("Forward", InputFlags::Forward)
        .bit("Shoot", InputFlags::Shoot);

    world.system<InputFlags>("input::tank")
        .kind(flecs::PreUpdate)
        .with<Local>()
        .each(Input::tank);
    world.system<InterfacePrevious, InterfacePage, WindowEvents>("input::screen")
        .kind(flecs::PreUpdate)
        .each(Input::screen);
}

void Input::tank(flecs::iter& it, size_t, InputFlags& flags) {
    const auto& page = it.world().get<InterfacePage>();
    bool ingame = page == InterfacePage::Ingame;

    if (ingame) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        flags.value = InputFlags::None;

        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    flags.value |= InputFlags::Forward;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  flags.value |= InputFlags::Backward;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  flags.value |= InputFlags::Left;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) flags.value |= InputFlags::Right;
    }

    const auto& events = it.world().get<WindowEvents>();
    for (const auto& event : events) {
        switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            if (ingame && event.key.key == SDLK_SPACE && !event.key.repeat) {
                flags.value |= InputFlags::Shoot;
            }
            break;
        }
    }
}

void Input::screen(flecs::iter& it, size_t, const InterfacePrevious& prev, InterfacePage& page, const WindowEvents& events) {
    for (const auto& event : events) {
        switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_ESCAPE && !event.key.repeat) {
                page = prev.page;
            }
            break;
        }
    }

    float mouseX, mouseY;
    bool mouseDown = SDL_GetMouseState(&mouseX, &mouseY) & SDL_BUTTON_LMASK;
    Clay_SetPointerState({
        .x = mouseX,
        .y = mouseY
    }, mouseDown);
}
