#include "input.h"

#include <clay.h>

#include "component/interface.h"
#include "component/network.h"

Input::Input(flecs::world& world) {
    world.component<InputFlags>()
        .bit("Left", InputFlags::Left)
        .bit("Right", InputFlags::Right)
        .bit("Backward", InputFlags::Backward)
        .bit("Forward", InputFlags::Forward)
        .bit("Shoot", InputFlags::Shoot);

    world.system<InputFlags>("input::tank").kind(flecs::PreUpdate).with<Local>().each(Input::tank);
    world.system<InterfacePrevious, InterfacePage, WindowEvents>("input::screen").kind(flecs::PostUpdate).each(Input::screen);
}

void Input::tank(flecs::iter& it, size_t i, InputFlags& flags) {
    const auto& page = it.world().get<InterfacePage>();
    const auto* capture = it.world().try_get<InputCapture>();
    bool typing = (capture != nullptr) && capture->active;
    bool ingame = page == InterfacePage::Ingame && !typing;

    flags.value = InputFlags::None;

    if (ingame) {
        const bool* keys = SDL_GetKeyboardState(nullptr);

        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
            flags.value |= InputFlags::Forward;
        }
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
            flags.value |= InputFlags::Backward;
        }
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
            flags.value |= InputFlags::Left;
        }
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) {
            flags.value |= InputFlags::Right;
        }
    }

    const auto& events = it.world().get<WindowEvents>();
    for (const auto& event : events) {
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
                if (ingame && event.key.key == SDLK_SPACE && !event.key.repeat) {
                    flags.value |= InputFlags::Shoot;
                }
                break;
            default:
                break;
        }
    }
}

void Input::screen(flecs::iter& it, size_t i, const InterfacePrevious& prev, InterfacePage& page, const WindowEvents& events) {
    const auto* capture = it.world().try_get<InputCapture>();
    bool typing = (capture != nullptr) && capture->active;
    for (const auto& event : events) {
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE && !event.key.repeat && !typing) {
                    page = prev.page;
                }
                break;
            default:
                break;
        }
    }

    float mouseX;
    float mouseY;
    bool mouseDown = (SDL_GetMouseState(&mouseX, &mouseY) & SDL_BUTTON_LMASK) != 0U;
    Clay_SetPointerState({.x = mouseX, .y = mouseY}, mouseDown);
    Clay_UpdateScrollContainers(false, {.x = 0, .y = 0}, it.delta_time());
}
