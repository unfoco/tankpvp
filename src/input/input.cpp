#include "input.h"

#include <clay.h>

#include <algorithm>

#include "component/interface.h"
#include "component/network.h"
#include "component/object.h"
#include "component/render.h"

Input::Input(flecs::world& world) {
    world.set<TouchOverlay>({});
    world.system("input::touch").kind(flecs::PreUpdate).run(Input::touch);
    world.system<InputState>("input::gather").kind(flecs::PreUpdate).with<Local>().each(Input::gather);
    world.system<InterfacePrevious, InterfacePage, WindowEvents>("input::screen").kind(flecs::PostUpdate).each(Input::screen);
}

void Input::touch(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto& overlay = world.get_mut<TouchOverlay>();
        overlay.primary_pressed = false;
        constexpr float STICK_RADIUS = 0.09F;

        const auto& events = world.get<WindowEvents>();
        for (const auto& event : events) {
            switch (event.type) {
                case SDL_EVENT_FINGER_DOWN: {
                    overlay.active = true;
                    glm::vec2 at{event.tfinger.x, event.tfinger.y};
                    auto finger = static_cast<uint64_t>(event.tfinger.fingerID);
                    const auto* page = world.try_get<InterfacePage>();
                    bool in_chat = page != nullptr && *page == InterfacePage::Chat;
                    float aspect = 1.0F;
                    int window_count = 0;
                    if (SDL_Window** windows = SDL_GetWindows(&window_count); windows != nullptr) {
                        if (window_count > 0) {
                            int ww = 0;
                            int wh = 0;
                            SDL_GetWindowSize(windows[0], &ww, &wh);
                            aspect = wh > 0 ? static_cast<float>(ww) / static_cast<float>(wh) : 1.0F;
                        }
                        SDL_free(windows);
                    }
                    float dx = (at.x - TouchOverlay::PRIMARY_X) * aspect;
                    float dy = at.y - TouchOverlay::PRIMARY_Y;
                    bool in_primary = (dx * dx) + (dy * dy) <= TouchOverlay::PRIMARY_RADIUS * TouchOverlay::PRIMARY_RADIUS;
                    bool in_swipe = at.y < 0.12F && at.x > 0.35F && at.x < 0.65F;
                    if (in_chat) {
                        if (!overlay.swipe_held) {
                            overlay.swipe_held = true;
                            overlay.swipe_finger = finger;
                            overlay.swipe_start = at.y;
                        }
                    } else if (in_primary && !overlay.primary_held) {
                        overlay.primary_held = true;
                        overlay.primary_pressed = true;
                        overlay.primary_finger = finger;
                    } else if (in_swipe && !overlay.swipe_held) {
                        overlay.swipe_held = true;
                        overlay.swipe_finger = finger;
                        overlay.swipe_start = at.y;
                    } else if (at.x < 0.5F && !overlay.stick_held) {
                        overlay.stick_held = true;
                        overlay.stick_finger = finger;
                        overlay.stick_center = at;
                        overlay.stick_vector = {0.0F, 0.0F};
                    }
                    break;
                }
                case SDL_EVENT_FINGER_MOTION: {
                    auto finger = static_cast<uint64_t>(event.tfinger.fingerID);
                    if (overlay.stick_held && finger == overlay.stick_finger) {
                        glm::vec2 at{event.tfinger.x, event.tfinger.y};
                        glm::vec2 v = (at - overlay.stick_center) / STICK_RADIUS;
                        float len = glm::length(v);
                        if (len > 1.0F) {
                            v /= len;
                        }
                        overlay.stick_vector = v;
                    } else if (overlay.swipe_held && finger == overlay.swipe_finger) {
                        const auto* page = world.try_get<InterfacePage>();
                        bool in_chat = page != nullptr && *page == InterfacePage::Chat;
                        bool ingame = page != nullptr && *page == InterfacePage::Ingame;
                        if (ingame && event.tfinger.y - overlay.swipe_start > 0.15F) {
                            overlay.swipe_held = false;
                            world.set(InterfacePage::Chat);
                        } else if (in_chat && overlay.swipe_start - event.tfinger.y > 0.15F) {
                            overlay.swipe_held = false;
                            if (auto* pending = world.try_get_mut<WindowEvents>()) {
                                SDL_Event escape{};
                                escape.type = SDL_EVENT_KEY_DOWN;
                                escape.key.key = SDLK_ESCAPE;
                                escape.key.scancode = SDL_SCANCODE_ESCAPE;
                                pending->push(escape);
                            }
                        }
                    }
                    break;
                }
                case SDL_EVENT_FINGER_UP:
                case SDL_EVENT_FINGER_CANCELED: {
                    auto finger = static_cast<uint64_t>(event.tfinger.fingerID);
                    if (overlay.stick_held && finger == overlay.stick_finger) {
                        overlay.stick_held = false;
                        overlay.stick_vector = {0.0F, 0.0F};
                    }
                    if (overlay.primary_held && finger == overlay.primary_finger) {
                        overlay.primary_held = false;
                    }
                    if (overlay.swipe_held && finger == overlay.swipe_finger) {
                        overlay.swipe_held = false;
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
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

        if (const auto* overlay = it.world().try_get<TouchOverlay>(); overlay != nullptr && overlay->active) {
            if (overlay->stick_held) {
                in.move.x = std::clamp(in.move.x + overlay->stick_vector.x, -1.0F, 1.0F);
                in.move.y = std::clamp(in.move.y - overlay->stick_vector.y, -1.0F, 1.0F);
            }
            if (overlay->primary_held) {
                in.buttons |= button::Primary;
            }
            if (overlay->primary_pressed) {
                in.buttons |= button::Primary;
                in.pressed |= button::Primary;
            }
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
    if (const auto* ui = it.world().try_get<UiScale>(); ui != nullptr && ui->dpi > 0.0F) {
        float to_layout = ui->density / ui->dpi;
        mouseX *= to_layout;
        mouseY *= to_layout;
    }
    Clay_SetPointerState({.x = mouseX, .y = mouseY}, mouseDown);
    Clay_UpdateScrollContainers(false, {.x = 0, .y = 0}, it.delta_time());
}
