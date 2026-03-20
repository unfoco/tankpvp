#include "interface.h"

Clay_RenderCommandArray Interface::ingame(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) {
    prev.page = InterfacePage::Pause;

    Clay_BeginLayout();

    CLAY({
        .id = CLAY_ID("HudContainer"),
        .layout = {
            .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_GROW() },
            .padding = { 20, 20, 20, 20 },      // Inner padding from screen edges
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        }
    }) {
        CLAY({
            .layout = { .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_FIT() }, .layoutDirection = CLAY_LEFT_TO_RIGHT }
        }) {
            CLAY_TEXT(Str("SCORE: 1040"), CLAY_TEXT_CONFIG({
                .textColor = { 255, 120, 100, 255 }, // Gold
                .fontSize = 24
            }));

            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW() } } }) {}

            if (Interface::button(state, CLAY_ID("BtnHudMenu"), "Menu")) {
                prev.page = page;
                page = InterfacePage::Pause;
            }
        }

        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_GROW() } } }) {}

        CLAY({
            .layout = { .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_FIT() }, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM} }
        }) {
            CLAY({
                .id = CLAY_ID("HealthBarContainer"),
                .layout = { .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(20) }, .padding = { 2, 2, 2, 2 } },
                .backgroundColor = { 30, 30, 30, 200 },
                .cornerRadius = CLAY_CORNER_RADIUS(4)
            }) {
                float hpPercent = 0.75f;
                CLAY({
                    .layout = { .sizing = { CLAY_SIZING_PERCENT(hpPercent), CLAY_SIZING_GROW() } },
                    .backgroundColor = { 50, 200, 50, 255 },
                    .cornerRadius = CLAY_CORNER_RADIUS(2)
                }) {}
            }

            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW() } } }) {}

            CLAY_TEXT(Str("AMMO: 12/60"), CLAY_TEXT_CONFIG({
                .textColor = { 0, 0, 0, 255 },
                .fontSize = 24
            }));
        }
    }

    return Clay_EndLayout();
}
