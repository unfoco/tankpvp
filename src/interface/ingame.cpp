#include "interface.h"

#include <algorithm>

#include "component/input.h"
#include "component/render.h"
#include "util/time.h"

static constexpr double CHAT_FADE_HOLD = 10.0;
static constexpr double CHAT_FADE_END = 10.5;

auto Interface::ingame(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    prev.page = InterfacePage::Pause;
    const auto& chatLog = it.world().get<ChatLog>();
    double now = util::now();

    for (const auto& ev : events) {
        if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_T) {
            page = InterfacePage::Chat;
        }
    }

    Clay_BeginLayout();

    CLAY({.id = CLAY_ID("HudContainer"), .layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()}, .padding = {20, 20, 20, 20}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        CLAY({.id = CLAY_ID("ChatPanel"),
              .layout = {
                  .sizing = {CLAY_SIZING_FIXED(520), CLAY_SIZING_FIT()},
                  .childGap = 4,
                  .layoutDirection = CLAY_TOP_TO_BOTTOM,
              }}) {
            int visibleTotal = 0;
            for (int i = 0; i < chatLog.count; ++i) {
                if (now - chatLog.time(i) < CHAT_FADE_END) {
                    ++visibleTotal;
                }
            }
            int skip = std::max(0, visibleTotal - 4);
            int seen = 0;
            for (int i = 0; i < chatLog.count; ++i) {
                double age = now - chatLog.time(i);
                if (age >= CHAT_FADE_END) {
                    continue;
                }
                if (seen++ < skip) {
                    continue;
                }
                float fade = std::clamp(age <= CHAT_FADE_HOLD ? 1.0F : static_cast<float>((CHAT_FADE_END - age) / (CHAT_FADE_END - CHAT_FADE_HOLD)), 0.0F, 1.0F);
                std::string wrapped = widget::wrap(state, chatLog.at(i), 32, 492);
                Clay_Sizing bubbleSizing = {.width = CLAY_SIZING_FIT(), .height = CLAY_SIZING_FIT()};
                bubbleSizing.width.size.minMax.max = 520;
                CLAY({.layout = {.sizing = bubbleSizing, .padding = {14, 14, 6, 6}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
                      .backgroundColor = {15, 15, 20, fade * 150.0F},
                      .cornerRadius = CLAY_CORNER_RADIUS(6),
                      .clip = {.horizontal = true}}) {
                    widget::rich(state, wrapped, 32, {.r = 255, .g = 255, .b = 255, .a = fade * 200.0F});
                }
            }
        }

        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()}}}) {}
    }

    if (const auto* overlay = it.world().try_get<TouchOverlay>(); overlay != nullptr && overlay->active) {
        Clay_Dimensions screen{0, 0};
        int window_count = 0;
        if (SDL_Window** windows = SDL_GetWindows(&window_count); windows != nullptr) {
            if (window_count > 0) {
                int pw = 0;
                int ph = 0;
                SDL_GetWindowSizeInPixels(windows[0], &pw, &ph);
                screen = {static_cast<float>(pw), static_cast<float>(ph)};
            }
            SDL_free(windows);
        }
        if (const auto* ui = it.world().try_get<UiScale>(); ui != nullptr && ui->dpi > 0.0F) {
            screen.width /= ui->dpi;
            screen.height /= ui->dpi;
        }
        float radius = 0.09F * screen.width;
        if (overlay->stick_held) {
            glm::vec2 base{overlay->stick_center.x * screen.width, overlay->stick_center.y * screen.height};
            CLAY({.id = CLAY_ID("TouchStickBase"),
                  .layout = {.sizing = {CLAY_SIZING_FIXED(radius * 2.0F), CLAY_SIZING_FIXED(radius * 2.0F)}},
                  .backgroundColor = {255, 255, 255, 24},
                  .cornerRadius = CLAY_CORNER_RADIUS(radius),
                  .floating = {.offset = {base.x - radius, base.y - radius}, .attachTo = CLAY_ATTACH_TO_ROOT}}) {}
            float nub = radius * 0.4F;
            glm::vec2 tip = base + (overlay->stick_vector * radius);
            CLAY({.id = CLAY_ID("TouchStickNub"),
                  .layout = {.sizing = {CLAY_SIZING_FIXED(nub * 2.0F), CLAY_SIZING_FIXED(nub * 2.0F)}},
                  .backgroundColor = {255, 255, 255, 70},
                  .cornerRadius = CLAY_CORNER_RADIUS(nub),
                  .floating = {.offset = {tip.x - nub, tip.y - nub}, .attachTo = CLAY_ATTACH_TO_ROOT}}) {}
        }
        glm::vec2 primary_center{TouchOverlay::PRIMARY_X * screen.width, TouchOverlay::PRIMARY_Y * screen.height};
        float primary_radius = TouchOverlay::PRIMARY_RADIUS * screen.height;
        Clay_Color primary_color = overlay->primary_held ? Clay_Color{255, 120, 90, 90} : Clay_Color{255, 255, 255, 24};
        CLAY({.id = CLAY_ID("TouchPrimary"),
              .layout = {.sizing = {CLAY_SIZING_FIXED(primary_radius * 2.0F), CLAY_SIZING_FIXED(primary_radius * 2.0F)}},
              .backgroundColor = primary_color,
              .cornerRadius = CLAY_CORNER_RADIUS(primary_radius),
              .floating = {.offset = {primary_center.x - primary_radius, primary_center.y - primary_radius}, .attachTo = CLAY_ATTACH_TO_ROOT}}) {}
    }

    widget::view(it.world(), state, events);

    auto cmds = Clay_EndLayout();
    widget::view_dispatch(it.world());
    return cmds;
}
