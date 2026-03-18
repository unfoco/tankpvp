#include "interface.h"

#include "component/network.h"

Clay_RenderCommandArray Interface::connect(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const EventQueue& queue) {
    auto& target = it.world().get_mut<NetworkTarget>();

    Clay_BeginLayout();

    CLAY({
        .id = CLAY_ID("ConnectContainer"),
        .layout = {
            .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_GROW() },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
            .childGap = 20
        },
        .backgroundColor = { 20, 20, 25, 255 }
    }) {
        CLAY({
            .id = CLAY_ID("ConnectForm"),
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(400), CLAY_SIZING_FIT() },
                .padding = { 30, 30, 30, 30 },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap = 16
            },
            .backgroundColor = { 35, 35, 40, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(12)
        }) {
            CLAY_TEXT(Str("JOIN SERVER"), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 24 }));

            InputStyle inputStyle = { .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_FIT() } };

            CLAY_TEXT(Str("Address"), CLAY_TEXT_CONFIG({ .textColor = { 200, 200, 200, 255 }, .fontSize = 14 }));
            Interface::input(state, queue, CLAY_ID("AddressInput"), target.address, "e.g. 127.0.0.1", inputStyle);

            CLAY_TEXT(Str("Port"), CLAY_TEXT_CONFIG({ .textColor = { 200, 200, 200, 255 }, .fontSize = 14 }));
            Interface::input(state, queue, CLAY_ID("PortInput"), target.port, "e.g. 5000", inputStyle);

            CLAY({
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_FIT() },
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .childGap = 10,
                }
            }) {
                if (Interface::button(state, CLAY_ID("BtnBack"), "Back")) {
                    prev.page = page;
                    page = InterfacePage::Main;
                }

                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_FIT() } } }) {}

                ButtonStyle connectBtn = { .color = { 70, 130, 255, 255 } };
                if (Interface::button(state, CLAY_ID("BtnDoConnect"), "Connect", connectBtn)) {
                    // todo
                }
            }
        }
    }

    return Clay_EndLayout();
}
