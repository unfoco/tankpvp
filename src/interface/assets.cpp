#include "interface.h"

#include <algorithm>
#include <string>

#include "component/asset.h"
#include "component/network.h"

auto Interface::assets(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray {
    prev.page = InterfacePage::Main;

    const auto* store = it.world().try_get<AssetStore>();
    const bool downloading = store != nullptr && store->downloading();
    float frac = 0.0F;
    std::string title = "LOADING";
    std::string counts = "preparing\xE2\x80\xA6";
    std::string pct;
    if (downloading) {
        title = "DOWNLOADING ASSETS";
        frac = std::clamp(store->progress(), 0.0F, 1.0F);
        counts = std::to_string(store->download_done) + " / " + std::to_string(store->download_count) + " files     " + std::to_string(store->download_have / 1024) + " / " + std::to_string(store->download_target / 1024) + " KB";
        pct = std::to_string(static_cast<int>(frac * 100.0F)) + "%";
    } else {
        counts = "building the world\xE2\x80\xA6";
    }

    bool cancel = false;
    for (const auto& ev : events) {
        if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_ESCAPE) {
            cancel = true;
        }
    }

    constexpr float BAR_W = 520.0F;
    const float fillW = std::max(2.0F, BAR_W * frac);

    Clay_BeginLayout();

    CLAY({.id = CLAY_ID("AssetsContainer"),
          .layout =
              {
                  .sizing = {CLAY_SIZING_GROW(), CLAY_SIZING_GROW()},
                  .padding = {0, 0, 0, 104},
                  .childGap = 18,
                  .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER},
                  .layoutDirection = CLAY_TOP_TO_BOTTOM,
              },
          .backgroundColor = {20, 20, 25, 255}}) {
        CLAY_TEXT(Str(state.intern(title)), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 255}, .fontSize = 48}));
        CLAY_TEXT(Str(state.intern(counts)), CLAY_TEXT_CONFIG({.textColor = {190, 190, 200, 255}, .fontSize = 32}));

        if (downloading) {
            CLAY({.id = CLAY_ID("AssetsTrack"),
                  .layout = {.sizing = {CLAY_SIZING_FIXED(BAR_W), CLAY_SIZING_FIXED(26)}, .padding = {3, 3, 3, 3}},
                  .backgroundColor = {40, 40, 48, 255},
                  .cornerRadius = {6, 6, 6, 6}}) {
                CLAY({.id = CLAY_ID("AssetsFill"),
                      .layout = {.sizing = {CLAY_SIZING_FIXED(fillW), CLAY_SIZING_GROW()}},
                      .backgroundColor = {90, 200, 120, 255},
                      .cornerRadius = {4, 4, 4, 4}}) {}
            }
            CLAY_TEXT(Str(state.intern(pct)), CLAY_TEXT_CONFIG({.textColor = {255, 255, 255, 255}, .fontSize = 32}));
        }
    }

    auto commands = Clay_EndLayout();

    if (cancel && downloading) {
        it.world().entity().add<RequestQuit>();
        page = InterfacePage::Main;
    }
    return commands;
}
