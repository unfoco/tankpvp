#include "interface.h"

#include "component/network.h"

Clay_RenderCommandArray Interface::connect(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) {
    auto& target = it.world().get_mut<NetworkTarget>();

    Clay_BeginLayout();

    CLAY({
        .id = CLAY_ID("ConnectContainer"),
        .layout = {
            .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_GROW() },
            .childGap = 20,
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .backgroundColor = { 20, 20, 25, 255 }
    }) {
        CLAY({
            .id = CLAY_ID("ConnectForm"),
            .layout = {
                .sizing = { CLAY_SIZING_FIXED(400), CLAY_SIZING_FIT() },
                .padding = { 30, 30, 30, 30 },
                .childGap = 16,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
            },
            .backgroundColor = { 35, 35, 40, 255 },
            .cornerRadius = CLAY_CORNER_RADIUS(12)
        }) {
            CLAY_TEXT(Str("JOIN SERVER"), CLAY_TEXT_CONFIG({ .textColor = { 255, 255, 255, 255 }, .fontSize = 24 }));

            InputStyle inputStyle = { .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_FIT() } };

            CLAY_TEXT(Str("Address"), CLAY_TEXT_CONFIG({ .textColor = { 200, 200, 200, 255 }, .fontSize = 14 }));
            Interface::input(state, events, CLAY_ID("AddressInput"), target.address, {
                .maxLength = 253, .placeholder = "e.g. 127.0.0.1",
                .allow = InputFilter::Address,
            }, inputStyle);

            CLAY_TEXT(Str("Port"), CLAY_TEXT_CONFIG({ .textColor = { 200, 200, 200, 255 }, .fontSize = 14 }));
            Interface::input(state, events, CLAY_ID("PortInput"), target.port, {
                .min = 1, .max = 65535, .maxLength = 5, .placeholder = "e.g. 5000",
                .allow = InputFilter::Unsigned,
            }, inputStyle);

            CLAY({
                .layout = {
                    .sizing = { CLAY_SIZING_GROW(), CLAY_SIZING_FIT() },
                    .childGap = 10,
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
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
