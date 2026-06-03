#include "interface.h"

auto Interface::server(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    auto& list = it.world().get_mut<ServerList>();
    bool editing = list.editing >= 0;
    bool valid = !list.draft.address.empty();

    bool save = false;
    bool cancel = false;
    bool remove = false;

    Clay_BeginLayout();

    CLAY({.id = CLAY_ID("ServerEditContainer"),
          .layout =
              {
                  .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()},
                  .childGap = 20,
                  .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                  .layoutDirection = CLAY_TOP_TO_BOTTOM,
              },
          .backgroundColor = {20, 20, 25, 255}}) {
        CLAY({.id = CLAY_ID("ServerEditForm"),
              .layout =
                  {
                      .sizing = {CLAY_SIZING_FIXED(440), CLAY_SIZING_FIT()},
                      .padding = {30, 30, 30, 30},
                      .childGap = 16,
                      .layoutDirection = CLAY_TOP_TO_BOTTOM,
                  },
              .backgroundColor = {35, 35, 40, 255},
              .cornerRadius = CLAY_CORNER_RADIUS(12)}) {
            CLAY_TEXT(Str(editing ? "EDIT SERVER" : "ADD SERVER"), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 255}, .fontSize = 32}));

            InputStyle inputStyle = {.sizing = {.width = CLAY_SIZING_GROW(), .height = CLAY_SIZING_FIT()}};

            CLAY_TEXT(Str("Name"), CLAY_TEXT_CONFIG({.textColor = {200, 200, 200, 255}, .fontSize = 32}));
            Interface::input(state, events, CLAY_ID("EditName"), list.draft.name,
                             {
                                 .maxLength = 32,
                                 .placeholder = "e.g. My Server",
                                 .allow = InputFilter::Printable,
                             },
                             inputStyle);

            CLAY_TEXT(Str("Address"), CLAY_TEXT_CONFIG({.textColor = {200, 200, 200, 255}, .fontSize = 32}));
            Interface::input(state, events, CLAY_ID("EditAddress"), list.draft.address,
                             {
                                 .maxLength = 253,
                                 .placeholder = "e.g. 127.0.0.1",
                                 .allow = InputFilter::Address,
                             },
                             inputStyle);

            CLAY_TEXT(Str("Port"), CLAY_TEXT_CONFIG({.textColor = {200, 200, 200, 255}, .fontSize = 32}));
            Interface::input(state, events, CLAY_ID("EditPort"), list.draft.port,
                             {
                                 .min = 1,
                                 .max = 65535,
                                 .maxLength = 5,
                                 .placeholder = "e.g. 5000",
                                 .allow = InputFilter::Unsigned,
                             },
                             inputStyle);

            CLAY({.layout = {
                      .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIT()},
                      .childGap = 10,
                      .layoutDirection = CLAY_LEFT_TO_RIGHT,
                  }}) {
                if (Interface::button(state, CLAY_ID("BtnEditCancel"), "Cancel")) {
                    cancel = true;
                }

                if (editing) {
                    if (Interface::button(state, CLAY_ID("BtnEditDelete"), "Delete", {.color = {.r = 170, .g = 60, .b = 60, .a = 255}})) {
                        remove = true;
                    }
                }

                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIT()}}}) {}

                ButtonStyle saveBtn = {.color = valid ? Clay_Color{.r = 70, .g = 130, .b = 255, .a = 255} : Clay_Color{.r = 50, .g = 50, .b = 55, .a = 255}};
                saveBtn.textColor = valid ? Clay_Color{.r = 255, .g = 255, .b = 255, .a = 255} : Clay_Color{.r = 255, .g = 255, .b = 255, .a = 100};
                if (Interface::button(state, CLAY_ID("BtnEditSave"), "Save", saveBtn) && valid) {
                    save = true;
                }
            }
        }
    }

    auto cmds = Clay_EndLayout();

    if (save) {
        if (editing && list.editing < static_cast<int>(list.entries.size())) {
            list.entries[list.editing] = list.draft;
        } else {
            list.entries.push_back(list.draft);
        }
        page = InterfacePage::Connect;
    } else if (remove) {
        if (list.editing >= 0 && list.editing < static_cast<int>(list.entries.size())) {
            list.entries.erase(list.entries.begin() + list.editing);
        }
        page = InterfacePage::Connect;
    } else if (cancel) {
        page = InterfacePage::Connect;
    }

    return cmds;
}
