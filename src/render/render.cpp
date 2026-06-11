#include "render.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "component/render.h"
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
    auto floor = world.entity("render::floor_phase").add<Rendering>().depends_on(camera);
    auto shadow = world.entity("render::shadow_phase").add<Rendering>().depends_on(floor);
    auto underlay = world.entity("render::underlay_phase").add<Rendering>().depends_on(shadow);
    auto props = world.entity("render::props_phase").add<Rendering>().depends_on(underlay);
    auto solid = world.entity("render::solid_phase").add<Rendering>().depends_on(props);
    auto overhead = world.entity("render::overhead_phase").add<Rendering>().depends_on(solid);
    auto ebegin = world.entity("render::ebegin_phase").add<Rendering>().depends_on(overhead);
    auto tanks = world.entity("render::tanks").add<Rendering>().depends_on(ebegin);
    auto bullets = world.entity("render::bullets").add<Rendering>().depends_on(tanks);
    auto eend = world.entity("render::eend_phase").add<Rendering>().depends_on(bullets);
    auto effects = world.entity("render::effects").add<Rendering>().depends_on(eend);
    auto post = world.entity("render::post_phase").add<Rendering>().depends_on(effects);
    auto wallstop = world.entity("render::wallstop_phase").add<Rendering>().depends_on(post);
    auto screenfx = world.entity("render::screenfx_phase").add<Rendering>().depends_on(wallstop);
    auto view = world.entity("render::view").add<Rendering>().depends_on(screenfx);
    auto present = world.entity("render::present").add<Rendering>().depends_on(view);

    world.system<RenderState>("render::start").kind(begin).each(Render::start);
    world.system<RenderState, Position>("render::camera").kind(camera).with<Local>().each(Render::camera);
    world.system<RenderState, const TileChunk>("render::floor").kind(floor).each(Render::floor);
    world.system("render::shadow").kind(shadow).run(Render::shadow);
    world.system<RenderState, const TileChunk>("render::solid").kind(solid).each(Render::solid);
    world.system<RenderState, const Position, const Rotation, const Sprite, const Color*, const Blend*, const Layer*>("render::underlay").kind(underlay).with<Decoration>().without<Dying>().each(Render::prop_under);
    world.system<RenderState, const Position, const Rotation, const Sprite, const Color*, const Blend*, const Layer*>("render::props").kind(props).with<Decoration>().without<Dying>().each(Render::prop_below);
    world.system<RenderState, const Position, const Rotation, const Sprite, const Color*, const Blend*, const Layer*>("render::overhead").kind(overhead).with<Decoration>().without<Dying>().each(Render::prop_above);
    world.system<RenderState>("render::entities_begin").kind(ebegin).each(Render::entities_begin);
    world.system<RenderState, const Position, const Rotation, const Sprite, const Color*, const Blend*>("render::sprite").kind(tanks).without<Decoration>().without<Dying>().each(Render::sprite);
    world.system<RenderState, Position>("render::bullet").kind(bullets).with<Bullet>().without<Latent>().each(Render::bullet);
    world.system<RenderState, const Particle>("render::particles").kind(bullets).each(Render::particles);
    world.system<const RenderState, const Position, const VisionBlocker>("render::smoke").kind(bullets).each(Render::smoke);
    world.system<RenderState>("render::entities_end").kind(eend).each(Render::entities_end);
    world.system<RenderState>("render::post").kind(post).each(Render::postprocess);
    world.system<RenderState, const TileChunk>("render::walls_top").kind(wallstop).each(Render::walls_top);
    world.system<RenderState, const Position, const Rotation, const Sprite, const Color*, const Blend*, const Layer*>("render::overhead_top").kind(wallstop).with<Decoration>().without<Dying>().each(Render::overhead_top);
    world.system<RenderState>("render::radar").kind(screenfx).each(Render::radar);
    world.system<RenderState, InterfaceCommands>("render::interface").kind(view).each(Render::interface);

    world.observer<const RequestEffect>("render::burst").event(flecs::OnSet).each(Render::burst);
    world.observer<const RequestParticles>("render::emit").event(flecs::OnSet).each(Render::emit);
    world.system<Particle>("render::age").kind(flecs::OnUpdate).each(Render::age);
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

    constexpr int SMOKE = 32;
    SDL_Texture* smokeTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, SMOKE, SMOKE);
    if (smokeTexture != nullptr) {
        SDL_SetTextureBlendMode(smokeTexture, SDL_BLENDMODE_BLEND);
        std::array<uint8_t, SMOKE * SMOKE * 4> pixels{};
        for (int y = 0; y < SMOKE; ++y) {
            for (int x = 0; x < SMOKE; ++x) {
                float dx = (static_cast<float>(x) + 0.5F - (SMOKE / 2.0F)) / (SMOKE / 2.0F);
                float dy = (static_cast<float>(y) + 0.5F - (SMOKE / 2.0F)) / (SMOKE / 2.0F);
                float fall = std::clamp(1.0F - std::sqrt((dx * dx) + (dy * dy)), 0.0F, 1.0F);
                size_t idx = (static_cast<size_t>(y) * SMOKE + static_cast<size_t>(x)) * 4;
                pixels[idx + 0] = 255;
                pixels[idx + 1] = 255;
                pixels[idx + 2] = 255;
                pixels[idx + 3] = static_cast<uint8_t>(fall * fall * 255.0F);
            }
        }
        SDL_UpdateTexture(smokeTexture, nullptr, pixels.data(), SMOKE * 4);
    }

    constexpr int AO = 48;
    SDL_Texture* occlusionTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, AO, AO);
    if (occlusionTexture != nullptr) {
        SDL_SetTextureBlendMode(occlusionTexture, SDL_BLENDMODE_BLEND);
        std::array<uint8_t, AO * AO * 4> pixels{};
        for (int y = 0; y < AO; ++y) {
            for (int x = 0; x < AO; ++x) {
                int edge = std::min(std::min(x, AO - 1 - x), std::min(y, AO - 1 - y));
                float a = std::clamp(static_cast<float>(edge) / (AO * 0.30F), 0.0F, 1.0F);
                size_t idx = (static_cast<size_t>(y) * AO + static_cast<size_t>(x)) * 4;
                pixels[idx + 3] = static_cast<uint8_t>(a * a * 150.0F);
            }
        }
        SDL_UpdateTexture(occlusionTexture, nullptr, pixels.data(), AO * 4);
    }

    SDL_Texture* fragments[10] = {};
    int fragmentCount = 0;
    for (int i = 0; i < 10; ++i) {
        std::string path = "asset/texture/tank/fragment" + std::to_string(i) + ".png";
        SDL_Texture* frag = IMG_LoadTexture(renderer, path.c_str());
        if (frag != nullptr) {
            fragments[fragmentCount++] = frag;
        }
    }

    it.world().set<WindowEvents>({
        .target = window,
    });
    RenderState state{
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
        .smokeTexture = smokeTexture,
        .occlusionTexture = occlusionTexture,
    };
    for (int i = 0; i < fragmentCount; ++i) {
        state.fragmentTextures[i] = fragments[i];
    }
    state.fragmentCount = fragmentCount;
    it.world().set<RenderState>(state);
    it.world().set<SpriteCache>({});
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

void Render::start(flecs::iter& it, size_t, RenderState& render) {
    ensure_targets(render);
    SDL_SetRenderTarget(render.target, render.curIsA ? render.frameA : render.frameB);
    float scale = SDL_GetWindowDisplayScale(render.window);
    SDL_SetRenderScale(render.target, scale, scale);
    Environment env{};
    static flecs::query<const Environment> env_q = it.world().query<const Environment>();
    env_q.each([&](const Environment& e) -> void { env = e; });
    SDL_SetRenderDrawColor(render.target, static_cast<Uint8>(env.bg_r), static_cast<Uint8>(env.bg_g), static_cast<Uint8>(env.bg_b), 0xFF);
    SDL_RenderClear(render.target);
}

void Render::finish(flecs::iter& it, size_t, RenderState& render) {
    SDL_Texture* cur = render.curIsA ? render.frameA : render.frameB;
    SDL_Texture* prev = render.curIsA ? render.frameB : render.frameA;

    float scale = SDL_GetWindowDisplayScale(render.window);

    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(render.window, &winW, &winH);

    static flecs::query<const Camera> cam_q = it.world().query_builder<const Camera>().with<Local>().build();
    Camera cam{};
    bool have_cam = false;
    cam_q.each([&](flecs::entity /*e*/, const Camera& c) -> void { cam = c; have_cam = true; });
    if (have_cam) {
        cur = Render::effects(render, cur, cam, winW, winH);
    }
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

    if (have_cam && cam.tint_a > 0.001F) {
        SDL_SetRenderDrawBlendMode(render.target, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(render.target, static_cast<Uint8>(cam.tint_r), static_cast<Uint8>(cam.tint_g), static_cast<Uint8>(cam.tint_b), static_cast<Uint8>(std::clamp(cam.tint_a, 0.0F, 1.0F) * 255.0F));
        SDL_RenderFillRect(render.target, &full);
    }
    if (have_cam && cam.flash > 0.001F) {
        SDL_SetRenderDrawBlendMode(render.target, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(render.target, 255, 255, 255, static_cast<Uint8>(std::clamp(cam.flash, 0.0F, 1.0F) * 255.0F));
        SDL_RenderFillRect(render.target, &full);
    }

    render.curIsA = !render.curIsA;
    SDL_RenderPresent(render.target);
}
