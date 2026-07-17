#include "input.h"

#include <clay.h>

#include "component/interface.h"
#include "component/network.h"
#include "component/object.h"

Input::Input(flecs::world& world) {
    world.system<InputState>("input::gather").kind(flecs::PreUpdate).with<Local>().each(Input::gather);
    world.system<InterfacePrevious, InterfacePage, WindowEvents>("input::screen").kind(flecs::PostUpdate).each(Input::screen);
}

void Input::gather(flecs::iter& it, size_t i, InputState& in) {
    const auto& page = it.world().get<InterfacePage>();
    const auto* capture = it.world().try_get<InputCapture>();
    bool typing = (capture != nullptr) && capture->active;
    bool ingame = page == InterfacePage::Ingame && !typing;

    in = InputState{};

    if (ingame) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
            in.move.y += 1.0F;
        }
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
            in.move.y -= 1.0F;
        }
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) {
            in.move.x += 1.0F;
        }
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
            in.move.x -= 1.0F;
        }

        float mx = 0.0F;
        float my = 0.0F;
        uint32_t mouse = SDL_GetMouseState(&mx, &my);
        if ((mouse & SDL_BUTTON_RMASK) != 0U) {
            in.buttons |= button::Secondary;
        }

        if (keys[SDL_SCANCODE_E]) {
            in.buttons |= button::Action0;
        }
        if (keys[SDL_SCANCODE_R]) {
            in.buttons |= button::Action1;
        }
        if (keys[SDL_SCANCODE_F]) {
            in.buttons |= button::Action2;
        }
        if (keys[SDL_SCANCODE_Q]) {
            in.buttons |= button::Action3;
        }

        const auto* ptr = it.world().try_get<Pointer>();
        const auto* self_pos = it.entity(i).try_get<Position>();
        if (ptr != nullptr && ptr->valid && self_pos != nullptr) {
            glm::vec2 d = ptr->world - self_pos->value;
            if (glm::dot(d, d) > 1e-6F) {
                in.aim = glm::normalize(d);
            }
        }
    }

    const auto& events = it.world().get<WindowEvents>();
    for (const auto& event : events) {
        bool primary_down = event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_SPACE && !event.key.repeat;
        if (ingame && primary_down) {
            in.buttons |= button::Primary;
            in.pressed |= button::Primary;
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
