#include "render.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>

#include "component/event.h"
#include "component/network.h"
#include "util/format.h"
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
    auto view = world.entity("render::view").add<Rendering>().depends_on(bullets);
    auto present = world.entity("render::present").add<Rendering>().depends_on(view);

    world.system<RenderState>("render::start").kind(begin).each(Render::start);
    world.system<RenderState, Position>("render::camera").kind(camera).with<Local>().each(Render::camera);
    world.system<RenderState, Color, Position, Rotation>("render::tank").kind(tanks).with<Tank>().without<Dying>().each(Render::tank);
    world.system<RenderState, Position>("render::bullet").kind(bullets).with<Bullet>().without<Latent>().each(Render::bullet);
    world.system<RenderState, InterfaceCommands>("render::interface").kind(view).each(Render::interface);
    world.system<RenderState>("render::finish").kind(present).each(Render::finish);

    flecs::entity pipeline = world.pipeline().with(flecs::System).with<Rendering>().cascade(flecs::DependsOn).build();
    world.set<RenderPipeline>({pipeline.id()});
}

static auto measure_text(Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) -> Clay_Dimensions {
    auto** f = static_cast<TTF_Font**>(userData);
    bool editMode = config->fontId == format::FONT_EDIT;
    auto length = static_cast<size_t>(text.length);

    format::Text state;
    for (const char* p = text.baseChars; p != nullptr && p < text.chars;) {
        if (format::is_escape(p, static_cast<size_t>(text.chars - p))) {
            p += 4;
        } else if (format::is_code(p, static_cast<size_t>(text.chars - p))) {
            format::apply(p[2], state);
            p += 3;
        } else {
            ++p;
        }
    }

    int maxW = 0;
    int lineH = 0;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || text.chars[i] == '\n') {
            int h = 0;
            int w = format::width(f[0], f[1], text.chars + start, i - start, config->fontSize, state, editMode, &h);
            maxW = std::max(maxW, w);
            lineH = std::max(lineH, h);
            start = i + 1;
        }
    }
    return {
        .width = static_cast<float>(maxW),
        .height = static_cast<float>(lineH),
    };
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

    Clay_SetMaxElementCount(16384);
    Clay_SetMaxMeasureTextCacheWordCount(32768);
    uint32_t minMem = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(minMem, malloc(minMem));

    Clay_Initialize(arena, {}, {.errorHandlerFunction = [](Clay_ErrorData err) -> void {
                        SDL_Log("Clay error: %.*s", err.errorText.length, err.errorText.chars);
                        exit(EXIT_FAILURE);
                    }});

    TTF_Init();
    static TTF_Font* fonts[2];
    fonts[0] = TTF_OpenFont("asset/font/normal.ttf", 16);
    fonts[1] = TTF_OpenFont("asset/font/italic.ttf", 16);
    if (fonts[0] == nullptr) {
        SDL_Log("Failed to load font: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    TTF_SetFontHinting(fonts[0], TTF_HINTING_MONO);
    if (fonts[1] != nullptr) {
        TTF_SetFontHinting(fonts[1], TTF_HINTING_MONO);
    }

    auto* textEngine = TTF_CreateRendererTextEngine(renderer);

    Clay_SetMeasureTextFunction(measure_text, static_cast<void*>(fonts));

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
                .fonts = fonts,
            },
        .tankBaseTexture = tankBaseTexture,
        .tankTurretTexture = tankTurretTexture,
        .weaponBulletTexture = weaponBulletTexture,
    });
}

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
        t = (tr->duration > 0.0) ? (util::now() - tr->start) / tr->duration : 2.0;
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
