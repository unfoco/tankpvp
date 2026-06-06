#include "interface.h"

#include "component/network.h"

auto Interface::status(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    prev.page = InterfacePage::Main;

    const auto* status = it.world().try_get<ConnectionStatus>();
    bool connecting = (status == nullptr) || status->state != ConnectionState::Disconnected;
    const char* title = connecting ? "CONNECTING" : "DISCONNECTED";
    const char* reason = (status != nullptr && !status->reason.empty()) ? status->reason.c_str() : nullptr;

    Clay_BeginLayout();

    CLAY({.id = CLAY_ID("StatusContainer"),
          .layout =
              {
                  .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()},
                  .padding = {0, 0, 0, 104},
                  .childGap = 16,
                  .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                  .layoutDirection = CLAY_TOP_TO_BOTTOM,
              },
          .backgroundColor = {20, 20, 25, 255}}) {
        CLAY_TEXT(Str(title), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 255}, .fontSize = 64}));

        if (reason != nullptr) {
            CLAY_TEXT(Str(reason), CLAY_TEXT_CONFIG({.textColor = {200, 200, 200, 255}, .fontSize = 32}));
        }

        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(0), CLAY_SIZING_FIXED(40)}}}) {}

        ButtonStyle menuBtn = {.fontSize = 32, .width = 520, .padding = {.left = 40, .right = 40, .top = 12, .bottom = 12}};

        const char* label = connecting ? "Cancel" : "Return";
        if (widget::button(state, CLAY_ID("BtnStatusReturn"), label, menuBtn)) {
            it.world().entity().add<RequestQuit>();
            page = InterfacePage::Main;
        }
    }

    return Clay_EndLayout();
}
