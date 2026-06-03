#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <flecs.h>

#include <cstdlib>
#include <string>

#include "component/event.h"
#include "component/object.h"
#include "component/settings.h"
#include "component/network.h"
#include "component/physics.h"

#include "input/input.h"
#include "logic/logic.h"
#include "render/render.h"
#include "physics/physics.h"
#include "network/network.h"
#include "interface/interface.h"

struct State {
    flecs::world world;
    bool headless = false;
    double accumulator = 0;
    uint64_t last_ticks = 0;
};

static NetworkConfig parse_args(int argc, char** argv, bool& headless) {
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
                cfg.port = (uint16_t)std::atoi(target.c_str() + colon + 1);
            } else {
                cfg.address = target;
            }
        } else if (arg == "--port" && i + 1 < argc) {
            cfg.port = (uint16_t)std::atoi(argv[++i]);
        }
    }
    if (cfg.role == NetworkRole::Server) headless = true;
    return cfg;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    State* state = new State();

    NetworkConfig cfg = parse_args(argc, argv, state->headless);
    state->world.set<NetworkConfig>(cfg);

    state->world.set<SimulationClock>({});

    state->world.set_threads(4);

    state->world.set<Settings>({
        .volume = 1.0,
        .test = true,
    });

    state->world.set<PhysicsConfig>({
        .gravity = {0.0f, 0.0f}
    });

    state->world.import<Physics>();
    state->world.import<Network>();
    state->world.import<Logic>();

    if (!state->headless) {
        state->world.import<Interface>();
        state->world.import<Render>();
        state->world.import<Input>();
    }

    state->world.component<Decay>()
        .member<float>("seconds");

    if (cfg.role == NetworkRole::Server) {
        state->world.entity().set(NetworkRequestHost{.address = cfg.address, .port = cfg.port});
    } else if (cfg.role == NetworkRole::Client) {
        state->world.entity().set(NetworkRequestJoin{.address = cfg.address, .port = cfg.port});
        if (!state->headless) state->world.set(InterfacePage::Ingame);
    }

    *appstate = state;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    State *state = (State*)appstate;

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    if (state->world.try_get<WindowEvents>()) {
        state->world.get_mut<WindowEvents>().push(*event);
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    State *state = (State*)appstate;

    auto tick_once = [&] {
        state->world.progress(TICK_DT);
        if (state->world.try_get<WindowEvents>()) {
            state->world.get_mut<WindowEvents>().clear();
        }
    };

    uint64_t now = SDL_GetTicks();
    if (state->last_ticks == 0) state->last_ticks = now;
    state->accumulator += (now - state->last_ticks) / 1000.0;
    state->last_ticks = now;
    if (state->accumulator > 0.25) state->accumulator = 0.25;

    int ticks = 0;
    for (double a = state->accumulator; a >= TICK_DT; a -= TICK_DT) ++ticks;
    for (int i = 0; i < ticks; ++i) {
        tick_once();
        state->accumulator -= TICK_DT;
    }

    if (!state->headless)
        state->world.run_pipeline(state->world.get<RenderPipeline>().value);
    if (state->headless) SDL_Delay(1);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    State *state = (State*)appstate;
}
