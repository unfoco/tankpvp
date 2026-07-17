#include "widget.h"
#include <cstring>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "component/interface.h"
#include "component/network.h"
#include "component/object.h"
#include "component/script.h"

static auto local_player(flecs::world world) -> flecs::entity {
    flecs::entity body;
    world.query_builder().with<Local>().build().each([&](flecs::entity e) -> void { body = e; });
    return body;
}

static auto read_field(ecs_primitive_kind_t kind, const void* ptr) -> float {
    switch (kind) {
        case EcsBool:
            return *static_cast<const bool*>(ptr) ? 1.0F : 0.0F;
        case EcsChar:
        case EcsByte:
        case EcsU8:
            return static_cast<float>(*static_cast<const uint8_t*>(ptr));
        case EcsI8:
            return static_cast<float>(*static_cast<const int8_t*>(ptr));
        case EcsU16:
            return static_cast<float>(*static_cast<const uint16_t*>(ptr));
        case EcsI16:
            return static_cast<float>(*static_cast<const int16_t*>(ptr));
        case EcsU32:
            return static_cast<float>(*static_cast<const uint32_t*>(ptr));
        case EcsI32:
            return static_cast<float>(*static_cast<const int32_t*>(ptr));
        case EcsU64:
            return static_cast<float>(*static_cast<const uint64_t*>(ptr));
        case EcsI64:
            return static_cast<float>(*static_cast<const int64_t*>(ptr));
        case EcsF32:
            return *static_cast<const float*>(ptr);
        case EcsF64:
            return static_cast<float>(*static_cast<const double*>(ptr));
        default:
            return 0.0F;
    }
}

static auto resolve_component(flecs::world world, const std::string& name) -> flecs::entity {
    flecs::entity found;
    world.query_builder().with<flecs::Component>().build().each([&](flecs::entity comp) -> void {
        if (!found && name == comp.name().c_str()) {
            found = comp;
        }
    });
    return found;
}

static auto resolve_number(flecs::world world, const std::string& path) -> float {
    flecs::entity e = local_player(world);
    if (!e || !e.is_alive()) {
        return 0;
    }
    size_t dot = path.find('.');
    if (dot == std::string::npos) {
        return 0;
    }
    flecs::entity comp = resolve_component(world, path.substr(0, dot));
    if (!comp || !ecs_has_id(world.c_ptr(), e.id(), comp)) {
        return 0;
    }
    const void* base = ecs_get_id(world.c_ptr(), e.id(), comp);
    const auto* layout = ecs_get(world.c_ptr(), comp, EcsStruct);
    if (base == nullptr || layout == nullptr) {
        return 0;
    }
    std::string field = path.substr(dot + 1);
    int32_t count = ecs_vec_count(&layout->members);
    for (int32_t i = 0; i < count; ++i) {
        auto* member = ecs_vec_get_t(&layout->members, ecs_member_t, i);
        if (field == member->name) {
            const auto* primitive = ecs_get(world.c_ptr(), member->type, EcsPrimitive);
            if (primitive != nullptr) {
                return read_field(primitive->kind, static_cast<const uint8_t*>(base) + member->offset);
            }
        }
    }
    return 0;
}

struct RenderContext {
    flecs::world world;
    InterfaceState& state;
    const WindowEvents& events;
    std::unordered_map<std::string, std::string>& values;
    std::string view;
};

static void render_widget(RenderContext& rc, const ViewWidget& node, int& index) {
    int self = index++;
    switch (node.kind) {
        case ViewKind::Panel: {
            const bool row = node.layout == ViewLayout::Row;
            const Clay_Sizing sizing = node.card ? Clay_Sizing{CLAY_SIZING_FIT(280), CLAY_SIZING_FIT()}
                                                 : Clay_Sizing{row ? CLAY_SIZING_GROW() : CLAY_SIZING_FIT(), CLAY_SIZING_FIT()};
            const Clay_Padding padding = node.card ? Clay_Padding{14, 14, 12, 12} : Clay_Padding{0, 0, 0, 0};
            const Clay_Color bg = node.card ? Clay_Color{static_cast<float>(node.bg_r), static_cast<float>(node.bg_g), static_cast<float>(node.bg_b), static_cast<float>(node.bg_a)}
                                            : Clay_Color{0.0F, 0.0F, 0.0F, 0.0F};
            CLAY({.id = CLAY_IDI("ViewNode", self),
                  .layout = {.sizing = sizing,
                             .padding = padding,
                             .childGap = 8,
                             .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                             .layoutDirection = row ? CLAY_LEFT_TO_RIGHT : CLAY_TOP_TO_BOTTOM},
                  .backgroundColor = bg,
                  .cornerRadius = CLAY_CORNER_RADIUS(node.card ? 8.0F : 0.0F)}) {
                if (!node.text.empty()) {
                    widget::rich(rc.state, "§l" + node.text, 32, {255, 255, 255, 255});
                }
                for (const auto& child : node.children) {
                    render_widget(rc, child, index);
                }
            }
            break;
        }
        case ViewKind::Label: {
            std::string text = node.text;
            if (!node.bind.empty()) {
                char tmp[32];
                std::snprintf(tmp, sizeof(tmp), "%.0f", static_cast<double>(resolve_number(rc.world, node.bind)));
                text = tmp;
            }
            for (size_t open = text.find('{'); open != std::string::npos; open = text.find('{', open)) {
                size_t close = text.find('}', open);
                if (close == std::string::npos) {
                    break;
                }
                std::string path = text.substr(open + 1, close - open - 1);
                char tmp[32];
                std::snprintf(tmp, sizeof(tmp), "%.0f", static_cast<double>(resolve_number(rc.world, path)));
                text.replace(open, close - open + 1, tmp);
                open += std::strlen(tmp);
            }
            const bool fixed = node.number > 0.0F;
            const Clay_LayoutAlignmentX align = fixed ? CLAY_ALIGN_X_LEFT : CLAY_ALIGN_X_CENTER;
            CLAY({.id = CLAY_IDI("ViewNode", self),
                  .layout = {.sizing = {fixed ? CLAY_SIZING_FIXED(node.number) : CLAY_SIZING_GROW(), CLAY_SIZING_FIT()},
                             .childAlignment = {.x = align}}}) {
                widget::rich(rc.state, text, 32, {235, 235, 235, 255}, align);
            }
            break;
        }
        case ViewKind::Bar: {
            float value = node.bind.empty() ? node.number : resolve_number(rc.world, node.bind);
            float max = node.bind_max.empty() ? node.number_max : resolve_number(rc.world, node.bind_max);
            float frac = (max > 0) ? std::clamp(value / max, 0.0F, 1.0F) : 0.0F;
            CLAY({.id = CLAY_IDI("ViewNode", self),
                  .layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIXED(18)}, .padding = {2, 2, 2, 2}},
                  .backgroundColor = {30, 30, 30, 220},
                  .cornerRadius = CLAY_CORNER_RADIUS(4)}) {
                if (frac > 0) {
                    CLAY({.layout = {.sizing = {CLAY_SIZING_PERCENT(frac), CLAY_SIZING_GROW()}},
                          .backgroundColor = {static_cast<float>(node.color_r), static_cast<float>(node.color_g), static_cast<float>(node.color_b), 255},
                          .cornerRadius = CLAY_CORNER_RADIUS(2)}) {}
                }
            }
            break;
        }
        case ViewKind::Minimap: {
            const float box = node.number > 0.0F ? node.number : 200.0F;
            auto* handle = rc.world.try_get_mut<MinimapHandle>();
            void* image = nullptr;
            if (handle != nullptr) {
                handle->size = box;
                handle->range = node.number_max > 1.0F ? node.number_max : 1700.0F;
                image = handle->image;
            }
            if (image != nullptr) {
                CLAY({.id = CLAY_IDI("ViewMinimap", self),
                      .layout = {.sizing = {CLAY_SIZING_FIXED(box), CLAY_SIZING_FIXED(box)}},
                      .cornerRadius = CLAY_CORNER_RADIUS(9.0F),
                      .image = {.imageData = image}}) {}
            } else {
                CLAY({.id = CLAY_IDI("ViewMinimap", self),
                      .layout = {.sizing = {CLAY_SIZING_FIXED(box), CLAY_SIZING_FIXED(box)}}}) {}
            }
            break;
        }
        case ViewKind::Input: {
            std::string& value = rc.values[node.field];
            InputStyle style = {.fontSize = 32, .sizing = {.width = CLAY_SIZING_FIXED(220), .height = CLAY_SIZING_FIT()}};
            widget::input(rc.state, rc.events, CLAY_IDI("ViewInput", self), value,
                             {.maxLength = 64, .commitOnEnter = false, .placeholder = node.text.c_str(), .allow = InputFilter::Printable}, style);
            break;
        }
        case ViewKind::Spacer:
            if (node.number > 0.0F) {
                CLAY({.id = CLAY_IDI("ViewNode", self), .layout = {.sizing = {CLAY_SIZING_FIXED(node.number), CLAY_SIZING_FIXED(node.number)}}}) {}
            } else {
                CLAY({.id = CLAY_IDI("ViewNode", self), .layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()}}}) {}
            }
            break;
        case ViewKind::Separator:
            CLAY({.id = CLAY_IDI("ViewNode", self),
                  .layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIXED(2)}},
                  .backgroundColor = {255, 255, 255, 40},
                  .cornerRadius = CLAY_CORNER_RADIUS(1)}) {}
            break;
        case ViewKind::Slider: {
            std::string& value = rc.values[node.field];
            float fv = value.empty() ? node.number : static_cast<float>(std::strtod(value.c_str(), nullptr));
            fv = std::clamp(fv, node.number, node.number_max);
            SliderStyle style;
            style.width = CLAY_SIZING_FIXED(200);
            widget::slider(rc.state, CLAY_IDI("ViewSlider", self), fv, node.number, node.number_max, style);
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "%g", static_cast<double>(fv));
            value = tmp;
            break;
        }
        case ViewKind::Toggle: {
            std::string& value = rc.values[node.field];
            bool on = value == "true";
            CLAY({.id = CLAY_IDI("ViewNode", self),
                  .layout = {.childGap = 10, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}, .layoutDirection = CLAY_LEFT_TO_RIGHT}}) {
                widget::toggle(rc.state, CLAY_IDI("ViewToggle", self), on);
                if (!node.text.empty()) {
                    widget::rich(rc.state, node.text, 32, {235, 235, 235, 255});
                }
            }
            value = on ? "true" : "false";
            break;
        }
        case ViewKind::Button: {
            ButtonStyle style{.color = {52, 52, 64, 255}, .grow = true, .padding = {.left = 14, .right = 14, .top = 7, .bottom = 7}};
            if (widget::button(rc.state, CLAY_IDI("ViewNode", self), node.text.c_str(), style) && node.handler != 0) {
                if (auto* view = rc.world.try_get_mut<ViewState>()) {
                    view->clicked = node.handler;
                    view->clicked_view = rc.view;
                }
            }
            break;
        }
    }
}

static auto placement_z(ViewPlacement p) -> int {
    switch (p) {
        case ViewPlacement::Center:
            return 3;
        case ViewPlacement::Bottom:
            return 2;
        default:
            return 1;
    }
}

void widget::view(flecs::world world, InterfaceState& state, const WindowEvents& events) {
    auto* views = world.try_get_mut<ViewState>();
    if (views == nullptr || views->views.empty()) {
        return;
    }
    std::vector<int> order(views->views.size());
    for (int i = 0; i < static_cast<int>(order.size()); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) -> bool {
        return placement_z(views->views[a].placement) < placement_z(views->views[b].placement);
    });
    int index = 0;
    for (int oi : order) {
        auto& active = views->views[oi];
        Clay_ChildAlignment align{.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER};
        bool modal = false;
        switch (active.placement) {
            case ViewPlacement::Center:
                modal = true;
                break;
            case ViewPlacement::TopRight:
                align = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_TOP};
                break;
            case ViewPlacement::BottomLeft:
                align = {.x = CLAY_ALIGN_X_LEFT, .y = CLAY_ALIGN_Y_BOTTOM};
                break;
            case ViewPlacement::Bottom:
                align = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM};
                break;
            case ViewPlacement::BottomRight:
                align = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_BOTTOM};
                break;
        }
        CLAY({.id = CLAY_IDI("ViewActive", index),
              .layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()}, .padding = {24, 24, 24, 24}, .childAlignment = align},
              .floating = {
                  .pointerCaptureMode = modal ? CLAY_POINTER_CAPTURE_MODE_CAPTURE : CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
                  .attachTo = CLAY_ATTACH_TO_ROOT,
              }}) {
            RenderContext rc{.world = world, .state = state, .events = events, .values = active.values, .view = active.id};
            render_widget(rc, active.root, index);
        }
    }
}

void widget::view_dispatch(flecs::world world) {
    auto* views = world.try_get_mut<ViewState>();
    if (views == nullptr || views->clicked == 0) {
        return;
    }
    uint32_t handler = views->clicked;
    views->clicked = 0;

    std::vector<std::pair<std::string, std::string>> values;
    for (const auto& s : views->views) {
        if (s.id == views->clicked_view) {
            for (const auto& [key, value] : s.values) {
                values.emplace_back(key, value);
            }
        }
    }

    world.entity().set(RequestViewClick{.handler = handler, .values = std::move(values)});
}
