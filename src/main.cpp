#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>

#include <flecs.h>

#include "component/event.h"
#include "component/input.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/settings.h"

#include "input/input.h"
#include "logic/logic.h"
#include "render/render.h"
#include "physics/physics.h"
#include "network/network.h"
#include "interface/interface.h"

struct State {
    flecs::world world;
};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    State* state = new State();

    // todo: set to core count?
    state->world.set_threads(4);

    state->world.set<WindowEvents>({});
    state->world.set<Settings>({
        .volume = 1.0,
        .test = true,
    });

    state->world.entity()
        .set(Color{.value = {255.0, 50.0, 50.0}})
        .set(Position{.value = {250.0, 400.0}})
        .set(Rotation{.angle = -100})
        .set(VelocityLinear{})
        .set(VelocityAngular{})
        .set(CollisionBox{.width = 40, .height = 30})
        .set(DampingLinear{.value = 5.0f})
        .set(DampingAngular{.value = 5.0f})
        .set(InputFlags::None)
        .add<Dynamic>()
        .add<Tank>()
        .add<Local>();

    for (int i = 0; i < 1000; i++) {
        state->world.entity()
            .set(Color{.value = {rand()%255, rand()%255, rand()%255}})
            .set(Position{.value = {200.0, 200.0}})
            .set(Rotation{.angle = 0})
            .set(VelocityLinear{})
            .set(VelocityAngular{})
            .set(CollisionBox{.width = 40, .height = 30})
            .set(DampingLinear{.value = 5.0f})
            .set(DampingAngular{.value = 5.0f})
            .set(InputFlags::Left)
            .add<Dynamic>()
            .add<Tank>();
    }

    state->world.import<Logic>();
    state->world.import<Input>();
    state->world.import<Render>();
    state->world.import<Physics>();
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

    auto& queue = state->world.get_mut<WindowEvents>();
    queue.push(*event);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    State *state = (State*)appstate;

    state->world.progress();

    auto& queue = state->world.get_mut<WindowEvents>();
    queue.clear();

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    State *state = (State*)appstate;
}
