#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <flecs.h>

#include <algorithm>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <string>

#include "component/event.h"
#include "component/network.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/render.h"
#include "component/settings.h"

#include "asset/asset.h"
#include "audio/audio.h"
#include "input/input.h"
#include "interface/interface.h"
#include "sim/sim.h"
#include "network/network.h"
#include "network/query.h"
#include "persist/persist.h"
#include "physics/physics.h"
#include "render/render.h"
#include "script/script.h"
#include "world/world.h"

struct State {
    flecs::world world;
    bool headless = false;
    double accumulator = 0;
    uint64_t last_ticks = 0;
};

static auto parse_args(int argc, char** argv, bool& headless, bool& netgraph) -> NetworkConfig {
    NetworkConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
            cfg.role = NetworkRole::Server;
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--netgraph") {
            netgraph = true;
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
    auto& world = state->world;

    world.set_threads(4);

    world.set<Settings>({
        .volume = 1.0F,
        .music = 1.0F,
    });

    world.set<PhysicsConfig>({.gravity = {0.0F, 0.0F}});

    bool netgraph = false;
    auto cfg = parse_args(argc, argv, state->headless, netgraph);
    world.set<NetworkConfig>(cfg);
    world.set<SimulationClock>({});
    if (netgraph) {
        world.add<NetworkDiagnose>();
    }

    world.import<Asset>();
    world.import<Physics>();
    world.import<World>();
    world.import<Network>();
    world.import<Sim>();
    world.import<Script>();

    if (!state->headless) {
        world.import<Interface>();
        world.import<Render>();
        world.import<Input>();
        world.import<Audio>();
        world.import<Persist>();
        world.import<NetworkQuery>();
    }

    world.component<Lifetime>().member<float>("seconds");

    if (cfg.role == NetworkRole::Server) {
        world.entity().set(RequestHost{.address = cfg.address, .port = cfg.port});
    } else if (cfg.role == NetworkRole::Client) {
        world.set<ConnectionStatus>({.state = ConnectionState::Connecting, .reason = ""});
        world.entity().set(RequestJoin{.address = cfg.address, .port = cfg.port});
        if (!state->headless) {
            world.set(InterfacePage::Status);
        }
    }

    *appstate = state;
    return SDL_APP_CONTINUE;
}

auto SDL_AppEvent(void* appstate, SDL_Event* event) -> SDL_AppResult {
    auto* state = static_cast<State*>(appstate);
    auto& world = state->world;

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (auto* events = world.try_get_mut<WindowEvents>()) {
        events->push(*event);
    }

    return SDL_APP_CONTINUE;
}

auto SDL_AppIterate(void* appstate) -> SDL_AppResult {
    auto* state = static_cast<State*>(appstate);
    auto& world = state->world;

    auto tick_once = [&] -> void {
        world.progress(TICK_DT);
        if (auto* events = world.try_get_mut<WindowEvents>()) {
            events->clear();
        }
    };

    uint64_t now = SDL_GetTicks();
    if (state->last_ticks == 0) state->last_ticks = now;
    double frame = static_cast<double>(now - state->last_ticks) / 1000.0;
    state->last_ticks = now;

    double step = TICK_DT;
    if (const auto* clock = world.try_get<SimulationClock>()) {
        step = TICK_DT / std::clamp(clock->scale, 0.9, 1.1);
    }

    if (frame > 0.35) {
        state->accumulator = step;
    } else {
        state->accumulator = std::min(state->accumulator + frame, 0.1);
    }

    int ticks = static_cast<int>(state->accumulator / step);
    for (int i = 0; i < ticks; ++i) {
        tick_once();
        state->accumulator -= step;
    }

    if (state->headless) {
        SDL_Delay(1);
    } else {
        world.set<FrameMix>({.alpha = static_cast<float>(std::clamp(state->accumulator / step, 0.0, 1.0))});
        world.run_pipeline(world.get<RenderPipeline>().value);
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* state = static_cast<State*>(appstate);
    auto& world = state->world;

    world.entity().add<RequestQuit>();
    world.progress(TICK_DT);
}
