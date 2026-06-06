#include "widget.h"

auto widget::toggle(InterfaceState& state, Clay_ElementId id, bool& value, ToggleStyle st) -> bool {
    bool toggled = false;
    float knob = st.height - (st.knobPad * 2);
    auto pad = static_cast<uint16_t>(st.knobPad);

    CLAY({
        .id = id,
        .layout =
            {
                .sizing = {CLAY_SIZING_FIXED(st.width), CLAY_SIZING_FIXED(st.height)},
                .padding = {pad, pad, pad, pad},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
        .backgroundColor = value ? st.onColor : st.offColor,
        .cornerRadius = CLAY_CORNER_RADIUS(st.height / 2),
    }) {
        if (Clay_Hovered()) {
            state.cursor = InterfaceCursor::Pointer;
            if (state.mousePressed) {
                value = !value;
                toggled = true;
            }
        }
        if (value) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW()}}}) {}
        }
        CLAY({
            .layout = {.sizing = {CLAY_SIZING_FIXED(knob), CLAY_SIZING_FIXED(knob)}},
            .backgroundColor = st.knobColor,
            .cornerRadius = CLAY_CORNER_RADIUS(knob / 2),
        }) {}
    }
    return toggled;
}
