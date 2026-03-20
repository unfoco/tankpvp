#include "interface.h"

Clay_RenderCommandArray Interface::pause(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) {
    prev.page = InterfacePage::Ingame;

    Clay_BeginLayout();

    CLAY({
        .id = CLAY_ID("MainMenuContainer"),
        .layout = {
            .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_GROW() },
            .padding = { 0, 0, 100, 0 },
            .childGap = 16,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .backgroundColor = { 20, 20, 25, 128 }
    }) {
        // Title
        CLAY_TEXT(Str("TANK GAME"), CLAY_TEXT_CONFIG({
            .textColor = { 255, 255, 255, 255 },
            .fontSize = 48
        }));

        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(0), CLAY_SIZING_FIXED(40) } } }) {}

        ButtonStyle menuBtn = {};
        menuBtn.padding = { 40, 40, 12, 12 };
        menuBtn.fontSize = 20;

        if (Interface::button(state, CLAY_ID("BtnPlay"), "Return Game", menuBtn)) {
            prev.page = page;
            page = InterfacePage::Ingame;
        }

        if (Interface::button(state, CLAY_ID("BtnSettings"), "Settings", menuBtn)) {
            prev.page = page;
            page = InterfacePage::Settings;
        }

        if (Interface::button(state, CLAY_ID("BtnQuit"), "Leave Game", menuBtn)) {
            // todo
        }
    }

    return Clay_EndLayout();
}
