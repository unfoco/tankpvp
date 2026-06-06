#include "interface.h"

#include <algorithm>

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
                const std::string& wrapped = state.intern(widget::wrap(state, chatLog.at(i), 32, 492));
                Clay_Sizing bubbleSizing = {.width = CLAY_SIZING_FIT(), .height = CLAY_SIZING_FIT()};
                bubbleSizing.width.size.minMax.max = 520;
                CLAY({.layout = {.sizing = bubbleSizing, .padding = {14, 14, 6, 6}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
                      .backgroundColor = {15, 15, 20, fade * 150.0F},
                      .cornerRadius = CLAY_CORNER_RADIUS(6),
                      .clip = {.horizontal = true}}) {
                    CLAY_TEXT(Str(wrapped), CLAY_TEXT_CONFIG({.textColor = {.r = 255, .g = 255, .b = 255, .a = fade * 200.0F}, .fontSize = 32, .wrapMode = CLAY_TEXT_WRAP_NEWLINES}));
                }
            }
        }

        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()}}}) {}
    }

    widget::view(it.world(), state, events);

    auto cmds = Clay_EndLayout();
    widget::view_dispatch(it.world());
    return cmds;
}
