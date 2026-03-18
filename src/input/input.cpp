#include "input.h"

#include <clay.h>

#include "component/event.h"
#include "component/object.h"

Input::Input(flecs::world& world) {
    world.system<InputFlags>("input::update")
        .kind(flecs::PostUpdate)
        .with<Local>()
        .each(Input::update);
}

void Input::update(flecs::iter& it, size_t, InputFlags& flags) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    flags = InputFlags::None;

    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    flags |= InputFlags::Forward;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  flags |= InputFlags::Backward;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  flags |= InputFlags::Left;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) flags |= InputFlags::Right;

    const auto& events = it.world().get<EventQueue>();
    for (const auto& event : events) {
        switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_SPACE && !event.key.repeat) {
                flags |= InputFlags::Shoot;
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
