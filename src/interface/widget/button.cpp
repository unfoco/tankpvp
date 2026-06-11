#include <algorithm>

#include "widget.h"

auto widget::button(InterfaceState& state, Clay_ElementId id, const char* label, ButtonStyle st) -> bool {
    bool clicked = false;
    Clay_Sizing sizing = {};
    if (st.grow) {
        sizing.width = CLAY_SIZING_GROW();
    } else if (st.width > 0) {
        sizing.width = CLAY_SIZING_FIXED(st.width);
    }
    auto scale = [](Clay_Color c, float f) -> Clay_Color {
        return {std::min(255.0F, c.r * f), std::min(255.0F, c.g * f), std::min(255.0F, c.b * f), c.a};
    };
    const Clay_Color hovered = scale(st.color, 1.25F);
    const Clay_Color pressed = scale(st.color, 1.60F);
    CLAY({
        .id = id,
        .layout = {.sizing = sizing, .padding = st.padding, .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = Clay_Hovered() ? (state.mouseDown ? pressed : hovered) : st.color,
        .cornerRadius = CLAY_CORNER_RADIUS(st.cornerRadius),
    }) {
        if (Clay_Hovered()) {
            state.cursor = InterfaceCursor::Pointer;
            if (state.mouseReleased) {
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
