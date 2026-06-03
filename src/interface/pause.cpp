#include "component/network.h"
#include "interface.h"

auto Interface::pause(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    prev.page = InterfacePage::Ingame;

    Clay_BeginLayout();

    CLAY({.id = CLAY_ID("PauseMenuContainer"),
          .layout =
              {
                  .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()},
                  .padding = {0, 0, 0, 104},
                  .childGap = 16,
                  .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                  .layoutDirection = CLAY_TOP_TO_BOTTOM,
              },
          .backgroundColor = {20, 20, 25, 128}}) {
        CLAY_TEXT(Str("TANK GAME"), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 255}, .fontSize = 64}));

        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(0), CLAY_SIZING_FIXED(40)}}}) {}

        ButtonStyle menuBtn = {};
        menuBtn.padding = {.left = 40, .right = 40, .top = 12, .bottom = 12};
        menuBtn.fontSize = 32;
        menuBtn.width = 520;

        if (Interface::button(state, CLAY_ID("BtnReturn"), "Return", menuBtn)) {
            prev.page = page;
            page = InterfacePage::Ingame;
        }

        if (Interface::button(state, CLAY_ID("BtnSettings"), "Settings", menuBtn)) {
            prev.page = page;
            page = InterfacePage::Settings;
        }

        if (Interface::button(state, CLAY_ID("BtnLeave"), "Leave", menuBtn)) {
            it.world().entity().add<NetworkRequestQuit>();
            page = InterfacePage::Main;
        }
    }

    return Clay_EndLayout();
}
