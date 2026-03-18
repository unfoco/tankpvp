#include "interface.h"

#include "component/settings.h"

Clay_RenderCommandArray Interface::settings(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const EventQueue& queue) {
    auto& settings = it.world().get_mut<Settings>();

    Clay_BeginLayout();

    CLAY({
        .id = CLAY_ID("SettingsContainer"),
        .layout = {
            .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_GROW() },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
            .childGap = 20
        },
        .backgroundColor = { 20, 20, 25, 255 }
    }) {
        CLAY({
            .id = CLAY_ID("SettingsPanel"),
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(500), CLAY_SIZING_FIT() },
                .padding = { 30, 30, 30, 30 },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap = 24
            },
            .backgroundColor = { 35, 35, 40, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(12)
        }) {
            CLAY_TEXT(Str("SETTINGS"), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 24 }));

            #define SETTING_ROW(id_string) CLAY({ \
                .id = CLAY_ID(id_string), \
                .layout = { \
                    .sizing = { \
                        .width = CLAY_SIZING_GROW(), \
                        .height = CLAY_SIZING_FIXED(30) \
                    }, \
                    .layoutDirection = CLAY_LEFT_TO_RIGHT, \
                    .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } \
                } \
            })

            InputStyle inputStyle = { .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_FIT() } };

            CLAY_TEXT(Str("Player Name"), CLAY_TEXT_CONFIG({ .textColor = { 200, 200, 200, 255 }, .fontSize = 14 }));
            Interface::input(state, queue, CLAY_ID("NameInput"), settings.username, "Enter name...", inputStyle);

            SETTING_ROW("RowVolume") {
                CLAY_TEXT(Str("Master Volume"), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 16 }));
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW() } } }) {}
                Interface::slider(state, CLAY_ID("VolSlider"), settings.volume, 0.0f, 1.0f, {});
            }

            SETTING_ROW("RowTest") {
                CLAY_TEXT(Str("Test"), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 16 }));
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW() } } }) {}
                if (Interface::toggle(state, CLAY_ID("FsToggle"), settings.test, {})) {
                    // test?
                }
            }

            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_FIXED(20) } } }) {}
            if (Interface::button(state, CLAY_ID("BtnBackSettings"), "Save & Back", { .color = { 70, 130, 255, 255 } })) {
                page = prev.page;
                prev.page = InterfacePage::Settings;
            }
        }
    }

    return Clay_EndLayout();
}
