#include "input.h"

#include <clay.h>

#include "component/event.h"
#include "component/object.h"
#include "component/interface.h"

Input::Input(flecs::world& world) {
    world.component<InputFlags>()
        .bit("Left", InputFlags::Left)
        .bit("Right", InputFlags::Right)
        .bit("Backward", InputFlags::Backward)
        .bit("Forward", InputFlags::Forward)
        .bit("Shoot", InputFlags::Shoot);

    world.system<InputFlags>("input::update")
        .kind(flecs::PostUpdate)
        .with<Local>()
        .each(Input::update);
}

void Input::update(flecs::iter& it, size_t, InputFlags& flags) {
    const auto& prev = it.world().get<InterfacePrevious>();
    auto& page = it.world().get_mut<InterfacePage>();

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
