#include "render.h"

#include "component/event.h"

#include <SDL3_image/SDL_image.h>

Render::Render(flecs::world& world) {
    world.component<RenderState>()
        .add(flecs::Singleton);

    world.system("render::init")
        .kind(flecs::OnStart)
        .each(Render::init);

    world.system<RenderState>("render::start")
        .kind(flecs::PreFrame)
        .each(Render::start);
    world.system<RenderState>("render::finish")
        .kind(flecs::PostFrame)
        .each(Render::finish);

    world.system<RenderState, InterfaceCommands>("render::interface")
        .kind(flecs::OnStore)
        .each(Render::interface);
    world.system<RenderState, Position>("render::camera")
        .kind(flecs::PostUpdate)
        .with<Local>()
        .each(Render::camera);

    world.system<RenderState, Position>("render::bullet")
        .kind(flecs::PostUpdate)
        .each(Render::bullet);
    world.system<RenderState, Color, Position, Rotation>("render::tank")
        .kind(flecs::PostUpdate)
        .with<Tank>()
        .each(Render::tank);
}

void Render::init(flecs::iter& it, size_t) {
    auto window = SDL_CreateWindow("Tank Trouble", WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    auto renderer = SDL_CreateRenderer(window, nullptr);

    if (!SDL_SetRenderVSync(renderer, true)) {
        SDL_Log("Failed to enable vsync");
        exit(EXIT_FAILURE);
    }

    float scale = SDL_GetWindowDisplayScale(window);
    SDL_SetRenderScale(renderer, scale, scale);

    uint32_t minMem = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(minMem, malloc(minMem));

    Clay_Initialize(arena, {}, {
        .errorHandlerFunction = [](Clay_ErrorData err) {
            SDL_Log("Clay error: %.*s", err.errorText.length, err.errorText.chars);
            exit(EXIT_FAILURE);
        }
    });

    TTF_Init();
    static TTF_Font* font;
    font = TTF_OpenFont("asset/font.ttf", 16);
    if (!font) {
        SDL_Log("Failed to load font: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    auto textEngine = TTF_CreateRendererTextEngine(renderer);

    Clay_SetMeasureTextFunction([](Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) -> Clay_Dimensions {
        auto** f = static_cast<TTF_Font**>(userData);
        TTF_SetFontSize(f[config->fontId], config->fontSize);

        int w, h;
        TTF_GetStringSize(f[config->fontId], text.chars, text.length, &w, &h);
        return {
            .width = static_cast<float>(w),
            .height = static_cast<float>(h),
        };
    }, &font);

    SDL_Texture* tankBaseTexture = IMG_LoadTexture(renderer, "asset/texture/tank/base.png");
    SDL_Texture* tankTurretTexture = IMG_LoadTexture(renderer, "asset/texture/tank/turret0.png");
    SDL_Texture* weaponBulletTexture = IMG_LoadTexture(renderer, "asset/texture/weapon/bullet.png");

    it.world().set<WindowEvents>({
        .target = window,
    });
    it.world().set(RenderState{
        .window = window,
        .target = renderer,
        .clay = {
            .renderer = renderer,
            .textEngine = textEngine,
            .fonts = &font,
        },
        .tankBaseTexture = tankBaseTexture,
        .tankTurretTexture = tankTurretTexture,
        .weaponBulletTexture = weaponBulletTexture,
    });
}

void Render::start(flecs::iter& it, size_t, const RenderState& render) {
    SDL_SetRenderDrawColor(render.target, 0xE6, 0xE6, 0xE6, 0xFF);
    SDL_RenderClear(render.target);
}

void Render::finish(flecs::iter& it, size_t, const RenderState& render) {
    SDL_RenderPresent(render.target);
}
