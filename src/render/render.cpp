#include "render.h"

#include <SDL3_image/SDL_image.h>

#include "component/event.h"
#include "component/network.h"
#include "util/time.h"

namespace {
struct Rendering {};
}

Render::Render(flecs::world& world) {
    world.component<RenderState>().add(flecs::Singleton);

    world.system("render::init").kind(flecs::OnStart).each(Render::init);

    auto begin = world.entity("render::begin").add<Rendering>();
    auto camera = world.entity("render::camera_phase").add<Rendering>().depends_on(begin);
    auto tanks = world.entity("render::tanks").add<Rendering>().depends_on(camera);
    auto bullets = world.entity("render::bullets").add<Rendering>().depends_on(tanks);
    auto ui = world.entity("render::ui").add<Rendering>().depends_on(bullets);
    auto present = world.entity("render::present").add<Rendering>().depends_on(ui);

    world.system<RenderState>("render::start").kind(begin).each(Render::start);
    world.system<RenderState, Position>("render::camera").kind(camera).with<Local>().each(Render::camera);
    world.system<RenderState, Color, Position, Rotation>("render::tank").kind(tanks).with<Tank>().without<Dying>().each(Render::tank);
    world.system<RenderState, Position>("render::bullet").kind(bullets).with<Bullet>().without<Latent>().each(Render::bullet);
    world.system<RenderState, InterfaceCommands>("render::interface").kind(ui).each(Render::interface);
    world.system<RenderState>("render::finish").kind(present).each(Render::finish);

    flecs::entity pipeline = world.pipeline().with(flecs::System).with<Rendering>().cascade(flecs::DependsOn).build();
    world.set<RenderPipeline>({pipeline.id()});
}

void Render::init(flecs::iter& it, size_t) {
    auto* window = SDL_CreateWindow("Tank Trouble", WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    auto* renderer = SDL_CreateRenderer(window, nullptr);

    if (!SDL_SetRenderVSync(renderer, 1)) {
        SDL_Log("Failed to enable vsync");
        exit(EXIT_FAILURE);
    }

    float scale = SDL_GetWindowDisplayScale(window);
    SDL_SetRenderScale(renderer, scale, scale);

    uint32_t minMem = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(minMem, malloc(minMem));

    Clay_Initialize(arena, {}, {.errorHandlerFunction = [](Clay_ErrorData err) -> void {
                        SDL_Log("Clay error: %.*s", err.errorText.length, err.errorText.chars);
                        exit(EXIT_FAILURE);
                    }});

    TTF_Init();
    static TTF_Font* font;
    font = TTF_OpenFont("asset/font.ttf", 16);
    if (font == nullptr) {
        SDL_Log("Failed to load font: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    TTF_SetFontHinting(font, TTF_HINTING_MONO);

    auto* textEngine = TTF_CreateRendererTextEngine(renderer);

    Clay_SetMeasureTextFunction(
        [](Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) -> Clay_Dimensions {
            auto** f = static_cast<TTF_Font**>(userData);
            TTF_SetFontSize(f[config->fontId], config->fontSize);

            int maxW = 0;
            int totalH = 0;
            size_t start = 0;
            for (size_t i = 0; i <= static_cast<size_t>(text.length); ++i) {
                if (i == static_cast<size_t>(text.length) || text.chars[i] == '\n') {
                    int w = 0;
                    int h = 0;
                    TTF_GetStringSize(f[config->fontId], text.chars + start, i - start, &w, &h);
                    maxW = std::max(maxW, w);
                    totalH += h;
                    start = i + 1;
                }
            }
            return {
                .width = static_cast<float>(maxW),
                .height = static_cast<float>(totalH),
            };
        },
        static_cast<void*>(&font));

    SDL_Texture* tankBaseTexture = IMG_LoadTexture(renderer, "asset/texture/tank/base.png");
    SDL_Texture* tankTurretTexture = IMG_LoadTexture(renderer, "asset/texture/tank/turret0.png");
    SDL_Texture* weaponBulletTexture = IMG_LoadTexture(renderer, "asset/texture/weapon/bullet.png");

    it.world().set<WindowEvents>({
        .target = window,
    });
    it.world().set(RenderState{
        .window = window,
        .target = renderer,
        .clay =
            {
                .renderer = renderer,
                .textEngine = textEngine,
                .fonts = &font,
            },
        .tankBaseTexture = tankBaseTexture,
        .tankTurretTexture = tankTurretTexture,
        .weaponBulletTexture = weaponBulletTexture,
    });
}

static constexpr double TRANSITION_DURATION = 0.1;

static void ensure_targets(RenderState& r) {
    int ow = 0;
    int oh = 0;
    SDL_GetCurrentRenderOutputSize(r.target, &ow, &oh);
    if (r.frameA != nullptr && ow == r.frameW && oh == r.frameH) {
        return;
    }
    SDL_DestroyTexture(r.frameA);
    SDL_DestroyTexture(r.frameB);
    SDL_DestroyTexture(r.snapshot);

    auto make = [&]() -> SDL_Texture* {
        SDL_Texture* t = SDL_CreateTexture(r.target, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow, oh);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(r.target, t);
        SDL_SetRenderDrawColor(r.target, 0, 0, 0, 255);
        SDL_RenderClear(r.target);
        return t;
    };
    r.frameA = make();
    r.frameB = make();
    r.snapshot = make();
    SDL_SetRenderTarget(r.target, nullptr);
    r.frameW = ow;
    r.frameH = oh;
}

void Render::start(flecs::iter&, size_t, RenderState& render) {
    ensure_targets(render);
    SDL_SetRenderTarget(render.target, render.curIsA ? render.frameA : render.frameB);
    float scale = SDL_GetWindowDisplayScale(render.window);
    SDL_SetRenderScale(render.target, scale, scale);
    SDL_SetRenderDrawColor(render.target, 0xE6, 0xE6, 0xE6, 0xFF);
    SDL_RenderClear(render.target);
}

void Render::finish(flecs::iter& it, size_t, RenderState& render) {
    SDL_Texture* cur = render.curIsA ? render.frameA : render.frameB;
    SDL_Texture* prev = render.curIsA ? render.frameB : render.frameA;

    float scale = SDL_GetWindowDisplayScale(render.window);

    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(render.window, &winW, &winH);
    float fw = static_cast<float>(winW);
    float fh = static_cast<float>(winH);
    SDL_FRect full = {.x = 0, .y = 0, .w = fw, .h = fh};

    double t = 2.0;
    const auto* tr = it.world().try_get<InterfaceTransition>();
    if (tr != nullptr) {
        if (tr->start != render.lastStart) {
            render.lastStart = tr->start;
            SDL_SetRenderTarget(render.target, render.snapshot);
            SDL_SetRenderScale(render.target, scale, scale);
            SDL_RenderTexture(render.target, prev, nullptr, &full);
        }
        t = (util::now() - tr->start) / TRANSITION_DURATION;
    }

    SDL_SetRenderTarget(render.target, nullptr);
    SDL_SetRenderScale(render.target, scale, scale);

    if (t < 0.0 || t >= 1.0 || tr == nullptr) {
        SDL_RenderTexture(render.target, cur, nullptr, &full);
    } else {
        float e = static_cast<float>(t);
        e = e * e * (3.0F - (2.0F * e));
        if (tr->kind == TransitionKind::Slide) {
            float dx = 0.0F;
            float dy = 0.0F;
            switch (tr->dir) {
                case TransitionDir::Left:
                    dx = -1.0F;
                    break;
                case TransitionDir::Right:
                    dx = 1.0F;
                    break;
                case TransitionDir::Up:
                    dy = -1.0F;
                    break;
                case TransitionDir::Down:
                    dy = 1.0F;
                    break;
            }
            SDL_FRect in = {.x = -dx * fw * (1.0F - e), .y = -dy * fh * (1.0F - e), .w = fw, .h = fh};
            SDL_FRect out = {.x = dx * fw * e, .y = dy * fh * e, .w = fw, .h = fh};
            SDL_RenderTexture(render.target, cur, nullptr, &in);
            SDL_RenderTexture(render.target, render.snapshot, nullptr, &out);
        } else {
            SDL_RenderTexture(render.target, cur, nullptr, &full);
            SDL_SetTextureAlphaMod(render.snapshot, static_cast<uint8_t>((1.0F - e) * 255.0F));
            SDL_RenderTexture(render.target, render.snapshot, nullptr, &full);
            SDL_SetTextureAlphaMod(render.snapshot, 255);
        }
    }

    render.curIsA = !render.curIsA;
    SDL_RenderPresent(render.target);
}
