#include "interface.h"

#include "component/settings.h"

auto Interface::settings(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    auto& settings = it.world().get_mut<Settings>();

    Clay_BeginLayout();

    CLAY({.id = CLAY_ID("SettingsContainer"),
          .layout =
              {
                  .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()},
                  .childGap = 20,
                  .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                  .layoutDirection = CLAY_TOP_TO_BOTTOM,
              },
          .backgroundColor = {20, 20, 25, 255}}) {
        CLAY({.id = CLAY_ID("SettingsPanel"),
              .layout =
                  {
                      .sizing = {CLAY_SIZING_FIXED(520), CLAY_SIZING_FIT()},
                      .padding = {30, 30, 30, 30},
                      .childGap = 24,
                      .layoutDirection = CLAY_TOP_TO_BOTTOM,
                  },
              .backgroundColor = {35, 35, 40, 255},
              .cornerRadius = CLAY_CORNER_RADIUS(12)}) {
            CLAY_TEXT(Str("SETTINGS"), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 255}, .fontSize = 32}));

#define SETTING_FIELD()                                                              \
    CLAY({.layout = {                                                                \
              .sizing = {.width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_FIT()},  \
              .childGap = 8,                                                         \
              .layoutDirection = CLAY_TOP_TO_BOTTOM,                                 \
          }})

            InputStyle inputStyle = {.sizing = {.width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_FIT()}};

            bool inSession = (prev.page == InterfacePage::Pause);

            SETTING_FIELD() {
                CLAY_TEXT(Str("Player Name"), CLAY_TEXT_CONFIG({.textColor = {200, 200, 200, 255}, .fontSize = 32}));
                widget::input(state, events, CLAY_ID("NameInput"), settings.username, {.maxLength = 16, .disabled = inSession, .placeholder = "Enter name...", .allow = InputFilter::Name}, inputStyle);
            }

            SETTING_FIELD() {
                CLAY_TEXT(Str("Master Volume"), CLAY_TEXT_CONFIG({.textColor = {200, 200, 200, 255}, .fontSize = 32}));
                widget::slider(state, CLAY_ID("VolSlider"), settings.volume, 0.0F, 1.0F, {});
            }

            SETTING_FIELD() {
                CLAY_TEXT(Str("Music Volume"), CLAY_TEXT_CONFIG({.textColor = {200, 200, 200, 255}, .fontSize = 32}));
                widget::slider(state, CLAY_ID("MusicSlider"), settings.music, 0.0F, 1.0F, {});
            }

            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIXED(20)}}}) {}
            if (widget::button(state, CLAY_ID("BtnBackSettings"), "Back")) {
                page = prev.page;
                prev.page = InterfacePage::Settings;
            }
        }
    }

    return Clay_EndLayout();
}
