#include "widget.h"

#include <algorithm>

auto widget::slider(InterfaceState& state, Clay_ElementId id, float& value, float lo, float hi, SliderStyle st) -> bool {
    float prev = value;
    float range = hi - lo;
    if (range <= 0) {
        return false;
    }

    if (state.activeId == id.id && state.mouseDown) {
        Clay_BoundingBox box = Clay_GetElementData(id).boundingBox;
        if (box.width > 0) {
            float fraction = (state.mouseX - box.x) / box.width;
            value = std::clamp(lo + (fraction * range), lo, hi);
        }
    }

    float norm = std::clamp((value - lo) / range, 0.F, 1.F);
    float trackWidth = Clay_GetElementData(id).boundingBox.width;

    CLAY({
        .id = id,
        .layout =
            {
                .sizing = {st.width, CLAY_SIZING_FIXED(st.trackHeight + 14)},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            },
    }) {
        if (Clay_Hovered()) {
            state.cursor = InterfaceCursor::Pointer;
            if (state.mousePressed) {
                state.activeId = id.id;
            }
        }
        CLAY({
            .layout =
                {
                    .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIXED(st.trackHeight)},
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                },
            .backgroundColor = st.trackColor,
            .cornerRadius = CLAY_CORNER_RADIUS(st.trackHeight / 2),
        }) {
            if (norm > 0 && trackWidth > 0) {
                CLAY({
                    .layout = {.sizing = {CLAY_SIZING_PERCENT(norm), CLAY_SIZING_FIXED(st.trackHeight)}},
                    .clip = {.horizontal = true},
                }) {
                    CLAY({
                        .layout = {.sizing = {CLAY_SIZING_FIXED(trackWidth), CLAY_SIZING_FIXED(st.trackHeight)}},
                        .backgroundColor = st.fillColor,
                        .cornerRadius = CLAY_CORNER_RADIUS(st.trackHeight / 2),
                    }) {}
                }
            }
        }
    }
    return value != prev;
}
