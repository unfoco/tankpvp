#include "interface.h"

Clay_RenderCommandArray Interface::main(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) {
    Clay_BeginLayout();

    CLAY({
        .id = CLAY_ID("MainMenuContainer"),
        .layout = {
            .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_GROW() },
            .padding = { 0, 0, 100, 0 },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
            .childGap = 16
        },
        .backgroundColor = { 20, 20, 25, 255 }
    }) {
        CLAY_TEXT(Str("TANK GAME"), CLAY_TEXT_CONFIG({
            .textColor = { 255, 255, 255, 255 },
            .fontSize = 48
        }));

        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(0), CLAY_SIZING_FIXED(40) } } }) {}

        ButtonStyle menuBtn = {};
        menuBtn.padding = { 40, 40, 12, 12 };
        menuBtn.fontSize = 20;

        if (Interface::button(state, CLAY_ID("BtnPlay"), "Host / Singleplayer", menuBtn)) {
            page = InterfacePage::Ingame;
        }

        if (Interface::button(state, CLAY_ID("BtnConnect"), "Join Multiplayer", menuBtn)) {
            prev.page = page;
            page = InterfacePage::Connect;
        }

        if (Interface::button(state, CLAY_ID("BtnSettings"), "Settings", menuBtn)) {
            prev.page = page;
            page = InterfacePage::Settings;
        }

        if (Interface::button(state, CLAY_ID("BtnQuit"), "Quit Game", menuBtn)) {
            // todo
        }
    }

    return Clay_EndLayout();
}
