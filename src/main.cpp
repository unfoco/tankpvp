#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>

#include <flecs.h>

#include "component/event.h"
#include "component/input.h"
#include "component/object.h"
#include "component/settings.h"

#include "input/input.h"
#include "logic/logic.h"
#include "render/render.h"
#include "network/network.h"
#include "interface/interface.h"

struct State {
    flecs::world world;
};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    State* state = new State();

    state->world.set<EventQueue>({});
    state->world.set<Settings>({
        .volume = 1.0,
        .test = true,
    });

    state->world.entity()
        .set(Color{.value = {255.0, 50.0, 50.0}})
        .set(Velocity{.value = {0.0, 0.0}})
        .set(Position{.value = {250.0, 400.0}})
        .set(Rotation{.angle = -100})
        .set(InputFlags::None)
        .add<Tank>()
        .add<Local>();

    state->world.entity()
        .set(Color{.value = {50.0, 200.0, 150.0}})
        .set(Velocity{.value = {0.0, 0.0}})
        .set(Position{.value = {200.0, 200.0}})
        .set(Rotation{.angle = 0})
        .set(InputFlags::Left)
        .add<Tank>();

    state->world.entity()
        .set(Color{.value = {0.0, 200.0, 200.0}})
        .set(Velocity{.value = {0.0, 0.0}})
        .set(Position{.value = {500.0, 250.0}})
        .set(Rotation{.angle = -100})
        .set(InputFlags::Right)
        .add<Tank>();

    state->world.import<Logic>();
    state->world.import<Input>();
    state->world.import<Render>();
    state->world.import<Network>();
    state->world.import<Interface>();

    *appstate = state;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    State *state = (State*)appstate;

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    auto& queue = state->world.get_mut<EventQueue>();
    queue.push(*event);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    State *state = (State*)appstate;

    state->world.progress();

    auto& queue = state->world.get_mut<EventQueue>();
    queue.clear();

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    State *state = (State*)appstate;
}
