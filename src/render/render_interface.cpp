#define CLAY_IMPLEMENTATION
#define CLAY_RENDERER_IMPLEMENTATION

#include "render.h"

void Render::interface(flecs::iter& it, size_t, const RenderState& render, InterfaceCommands& commands) {
    int width, height;
    SDL_GetWindowSize(render.window, &width, &height);

    Clay_SetLayoutDimensions({
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
    });

    if (commands.list.length == 0) return;
    SDL_Clay_Render(render.clay, commands.list);
}
