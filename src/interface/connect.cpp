#include <algorithm>
#include <string>

#include "component/network.h"
#include "interface.h"

auto Interface::connect(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    auto& list = it.world().get_mut<ServerList>();

    constexpr float kRowHeight = 64;
    constexpr uint16_t kRowGap = 8;
    constexpr float kListHeight = 360;

    int count = static_cast<int>(list.entries.size());
    float content = (count > 0) ? (static_cast<float>(count) * kRowHeight + static_cast<float>(count - 1) * kRowGap) : 0;
    float maxScroll = std::max(0.0F, content - kListHeight);

    for (const auto& ev : events) {
        if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
            list.scroll -= ev.wheel.y * kRowHeight;
        }
    }
    list.scroll = std::clamp(list.scroll, 0.0F, maxScroll);

    int connectIdx = -1;
    int editIdx = -1;
    bool add = false;

    Clay_BeginLayout();

    CLAY({.id = CLAY_ID("ConnectContainer"),
          .layout =
              {
                  .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()},
                  .childGap = 20,
                  .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                  .layoutDirection = CLAY_TOP_TO_BOTTOM,
              },
          .backgroundColor = {20, 20, 25, 255}}) {
        CLAY({.id = CLAY_ID("ConnectPanel"),
              .layout =
                  {
                      .sizing = {CLAY_SIZING_FIXED(560), CLAY_SIZING_FIT()},
                      .padding = {30, 30, 30, 30},
                      .childGap = 16,
                      .layoutDirection = CLAY_TOP_TO_BOTTOM,
                  },
              .backgroundColor = {35, 35, 40, 255},
              .cornerRadius = CLAY_CORNER_RADIUS(12)}) {
            CLAY_TEXT(Str("CONNECT SERVER"), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 255}, .fontSize = 32}));

            CLAY({.id = CLAY_ID("ServerListView"),
                  .layout =
                      {
                          .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIXED(kListHeight)},
                          .childGap = kRowGap,
                          .layoutDirection = CLAY_TOP_TO_BOTTOM,
                      },
                  .clip = {.vertical = true, .childOffset = {0, -list.scroll}}}) {
                if (count == 0) {
                    CLAY_TEXT(Str("No saved servers. Add one below."),
                              CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 120}, .fontSize = 32}));
                }

                for (int i = 0; i < count; ++i) {
                    const ServerEntry& entry = list.entries[i];
                    const std::string& name = entry.name.empty() ? entry.address : entry.name;
                    const std::string& sub = state.intern(entry.address + ":" + std::to_string(entry.port));

                    CLAY({.id = CLAY_IDI("ServerRow", i),
                          .layout =
                              {
                                  .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIXED(kRowHeight)},
                                  .padding = {14, 14, 0, 0},
                                  .childGap = 8,
                                  .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                                  .layoutDirection = CLAY_LEFT_TO_RIGHT,
                              },
                          .backgroundColor = {46, 46, 54, 255},
                          .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
                        CLAY({.layout = {
                                  .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIT()},
                                  .childGap = 2,
                                  .layoutDirection = CLAY_TOP_TO_BOTTOM,
                              }}) {
                            CLAY_TEXT(Str(name), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 255}, .fontSize = 32, .wrapMode = CLAY_TEXT_WRAP_NONE}));
                            CLAY_TEXT(Str(sub), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 130}, .fontSize = 16, .wrapMode = CLAY_TEXT_WRAP_NONE}));
                        }

                        ButtonStyle rowBtn = {.fontSize = 32, .padding = {.left = 14, .right = 14, .top = 8, .bottom = 8}};
                        ButtonStyle connectBtn = rowBtn;
                        connectBtn.color = {.r = 70, .g = 130, .b = 255, .a = 255};

                        if (Interface::button(state, CLAY_IDI("SrvConnect", i), "Connect", connectBtn)) {
                            connectIdx = i;
                        }
                        if (Interface::button(state, CLAY_IDI("SrvEdit", i), "Edit", rowBtn)) {
                            editIdx = i;
                        }
                    }
                }
            }

            CLAY({.layout = {
                      .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIT()},
                      .childGap = 10,
                      .layoutDirection = CLAY_LEFT_TO_RIGHT,
                  }}) {
                if (Interface::button(state, CLAY_ID("BtnBack"), "Back")) {
                    prev.page = page;
                    page = InterfacePage::Main;
                }

                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_FIT()}}}) {}

                if (Interface::button(state, CLAY_ID("BtnAddServer"), "+ Add Server", {.color = {.r = 70, .g = 130, .b = 255, .a = 255}})) {
                    add = true;
                }
            }
        }
    }

    auto cmds = Clay_EndLayout();

    if (editIdx >= 0 && editIdx < static_cast<int>(list.entries.size())) {
        list.editing = editIdx;
        list.draft = list.entries[editIdx];
        prev.page = page;
        page = InterfacePage::Server;
    } else if (add) {
        list.editing = -1;
        list.draft = {};
        prev.page = page;
        page = InterfacePage::Server;
    } else if (connectIdx >= 0) {
        const ServerEntry& entry = list.entries[connectIdx];
        it.world().get_mut<NetworkTarget>() = {.address = entry.address, .port = entry.port};
        it.world().set<ConnectionStatus>({.state = ConnectionState::Connecting, .reason = ""});
        it.world().entity().set(NetworkRequestJoin{.address = entry.address, .port = entry.port});
        page = InterfacePage::Status;
    }

    return cmds;
}
