#include "interface.h"

#include "component/network.h"

auto Interface::status(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    prev.page = InterfacePage::Connect;

    const auto* status = it.world().try_get<ConnectionStatus>();
    bool connecting = (status == nullptr) || status->state != ConnectionState::Disconnected;
    const char* title = connecting ? "CONNECTING" : "DISCONNECTED";
    const char* reason = (status != nullptr && !status->reason.empty()) ? status->reason.c_str() : nullptr;

    bool leave = false;
    for (const auto& ev : events) {
        if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_ESCAPE) {
            leave = true;
        }
    }

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
        widget::rich(state, title, 64, {255, 255, 255, 255}, CLAY_ALIGN_X_CENTER);

        if (reason != nullptr) {
            widget::rich(state, reason, 32, {200, 200, 200, 255}, CLAY_ALIGN_X_CENTER);
        }

        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(0), CLAY_SIZING_FIXED(40)}}}) {}

        ButtonStyle menuBtn = {.fontSize = 32, .width = 520, .padding = {.left = 40, .right = 40, .top = 12, .bottom = 12}};

        const char* label = connecting ? "Cancel" : "Return";
        if (widget::button(state, CLAY_ID("BtnStatusReturn"), label, menuBtn)) {
            leave = true;
        }
    }

    auto commands = Clay_EndLayout();

    if (leave) {
        it.world().entity().add<RequestQuit>();
        page = InterfacePage::Connect;
    }
    return commands;
}
