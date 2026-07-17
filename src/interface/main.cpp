#include "interface.h"

#include <cstdlib>

auto Interface::main(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    prev.page = InterfacePage::Main;
    Clay_BeginLayout();

    CLAY({.id = CLAY_ID("MainMenuContainer"),
          .layout =
              {
                  .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()},
                  .padding = {0, 0, 0, 104},
                  .childGap = 16,
                  .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                  .layoutDirection = CLAY_TOP_TO_BOTTOM,
              },
          .backgroundColor = {20, 20, 25, 255}}) {
        CLAY_TEXT(Str("EMBRIK"), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 255}, .fontSize = 64}));

        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(0), CLAY_SIZING_FIXED(40)}}}) {}

        ButtonStyle menuBtn = {.fontSize = 32, .width = 520, .padding = {.left = 40, .right = 40, .top = 12, .bottom = 12}};

        if (widget::button(state, CLAY_ID("BtnHost"), "Host", menuBtn)) {
            prev.page = page;
            page = InterfacePage::Host;
        }
        if (widget::button(state, CLAY_ID("BtnConnect"), "Connect", menuBtn)) {
            prev.page = page;
            page = InterfacePage::Connect;
        }
        if (widget::button(state, CLAY_ID("BtnSettings"), "Settings", menuBtn)) {
            prev.page = page;
            page = InterfacePage::Settings;
        }
        if (widget::button(state, CLAY_ID("BtnQuit"), "Quit", menuBtn)) {
            exit(0);
        }
    }

    return Clay_EndLayout();
}
