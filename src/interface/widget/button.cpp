#include "widget.h"

auto widget::button(InterfaceState& state, Clay_ElementId id, const char* label, ButtonStyle st) -> bool {
    bool clicked = false;
    Clay_Sizing sizing = {};
    if (st.width > 0) {
        sizing.width = CLAY_SIZING_FIXED(st.width);
    }
    CLAY({
        .id = id,
        .layout = {.sizing = sizing, .padding = st.padding, .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = st.color,
        .cornerRadius = CLAY_CORNER_RADIUS(st.cornerRadius),
    }) {
        if (Clay_Hovered()) {
            state.cursor = InterfaceCursor::Pointer;
            if (state.mousePressed) {
                clicked = true;
            }
        }
        CLAY_TEXT(Str(label), CLAY_TEXT_CONFIG({
                                  .textColor = st.textColor,
                                  .fontSize = st.fontSize,
                                  .wrapMode = CLAY_TEXT_WRAP_NONE,
                              }));
    }
    return clicked;
}
