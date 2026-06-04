#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <flecs.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string>

#include "component/event.h"
#include "component/network.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/settings.h"
#include "audio/audio.h"
#include "input/input.h"
#include "interface/interface.h"
#include "logic/logic.h"
#include "network/network.h"
#include "persist/persist.h"
#include "physics/physics.h"
#include "render/render.h"

struct State {
    flecs::world world;
    bool headless = false;
    double accumulator = 0;
    uint64_t last_ticks = 0;
};

static auto parse_args(int argc, char** argv, bool& headless) -> NetworkConfig {
    NetworkConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
            cfg.role = NetworkRole::Server;
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--connect" && i + 1 < argc) {
            cfg.role = NetworkRole::Client;
            std::string target = argv[++i];
            if (auto colon = target.find(':'); colon != std::string::npos) {
                cfg.address = target.substr(0, colon);
                const char* first = target.c_str() + colon + 1;
                const char* last = target.c_str() + target.size();
                uint16_t parsed = 0;
                if (std::from_chars(first, last, parsed).ec == std::errc{}) {
                    cfg.port = parsed;
                }
            } else {
                cfg.address = target;
            }
        } else if (arg == "--port" && i + 1 < argc) {
            const char* value = argv[++i];
            uint16_t parsed = 0;
            if (std::from_chars(value, value + std::char_traits<char>::length(value), parsed).ec == std::errc{}) {
                cfg.port = parsed;
            }
        }
    }
    if (cfg.role == NetworkRole::Server) {
        headless = true;
    }
    return cfg;
}

auto SDL_AppInit(void** appstate, int argc, char** argv) -> SDL_AppResult {
    auto* state = new State();

    NetworkConfig cfg = parse_args(argc, argv, state->headless);
    state->world.set<NetworkConfig>(cfg);

    state->world.set<SimulationClock>({});

    state->world.set_threads(4);

    state->world.set<Settings>({
        .volume = 1.0F,
        .music = 1.0F,
    });

    state->world.set<PhysicsConfig>({.gravity = {0.0F, 0.0F}});

    state->world.import<Physics>();
    state->world.import<Network>();
    state->world.import<Logic>();

    if (!state->headless) {
        state->world.import<Interface>();
        state->world.import<Render>();
        state->world.import<Input>();
        state->world.import<Audio>();
        state->world.import<Persist>();
    }

    state->world.component<Decay>().member<float>("seconds");

    if (cfg.role == NetworkRole::Server) {
        state->world.entity().set(NetworkRequestHost{.address = cfg.address, .port = cfg.port});
    } else if (cfg.role == NetworkRole::Client) {
        state->world.set<ConnectionStatus>({.state = ConnectionState::Connecting, .reason = ""});
        state->world.entity().set(NetworkRequestJoin{.address = cfg.address, .port = cfg.port});
        if (!state->headless) {
            state->world.set(InterfacePage::Status);
        }
    }

    *appstate = state;
    return SDL_APP_CONTINUE;
}

auto SDL_AppEvent(void* appstate, SDL_Event* event) -> SDL_AppResult {
    auto* state = static_cast<State*>(appstate);

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (state->world.try_get<WindowEvents>() != nullptr) {
        state->world.get_mut<WindowEvents>().push(*event);
    }

    return SDL_APP_CONTINUE;
}

auto SDL_AppIterate(void* appstate) -> SDL_AppResult {
    auto* state = static_cast<State*>(appstate);

    auto tick_once = [&] -> void {
        state->world.progress(TICK_DT);
        if (state->world.try_get<WindowEvents>()) {
            state->world.get_mut<WindowEvents>().clear();
        }
    };

    uint64_t now = SDL_GetTicks();
    if (state->last_ticks == 0) {
        state->last_ticks = now;
    }
    state->accumulator += static_cast<double>(now - state->last_ticks) / 1000.0;
    state->last_ticks = now;
    state->accumulator = std::min(state->accumulator, 0.25);

    int ticks = static_cast<int>(state->accumulator / static_cast<double>(TICK_DT));
    for (int i = 0; i < ticks; ++i) {
        tick_once();
        state->accumulator -= TICK_DT;
    }

    if (!state->headless) {
        state->world.run_pipeline(state->world.get<RenderPipeline>().value);
    }
    if (state->headless) {
        SDL_Delay(1);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* state = static_cast<State*>(appstate);
}
