#pragma once

#include <SDL3/SDL.h>

#include <webgpu/webgpu.hpp>
#include <webgpu/webgpu-raii.hpp>
#include <clay/clay_renderer_webgpu.h>

#include <flecs.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "component/interface.h"
#include "component/network.h"
#include "component/object.h"
#include "component/render.h"

#include "gpu_types.h"

constexpr auto WIDTH = 1280;
constexpr auto HEIGHT = 720;

constexpr uint32_t PARTICLE_CAP = 1U << 16;
constexpr uint32_t MAX_EMITTERS = 256;
constexpr uint32_t MAX_LIGHTS = 1024;
constexpr uint32_t MAX_OCCLUDERS = 4096;
constexpr uint32_t BLOOM_MIPS = 6;
constexpr uint32_t TILE_TEXELS = 32;
constexpr uint32_t PARTICLE_TEXELS = 64;
constexpr float OCCLUDER_PAD = 320.0F;


constexpr wgpu::TextureFormat FORMAT_SCENE = wgpu::TextureFormat::RGBA16Float;
constexpr wgpu::TextureFormat FORMAT_AUX = wgpu::TextureFormat::RGBA8Unorm;
constexpr wgpu::TextureFormat FORMAT_OCCLUDER = wgpu::TextureFormat::R8Unorm;
constexpr wgpu::TextureFormat FORMAT_LIGHT = wgpu::TextureFormat::RGBA16Float;
constexpr wgpu::TextureFormat FORMAT_DISTORTION = wgpu::TextureFormat::RG16Float;
constexpr wgpu::TextureFormat FORMAT_LDR = wgpu::TextureFormat::RGBA8Unorm;

using SortKey = uint64_t;

inline auto sort_key(const RenderDepth& depth, float y_relative, uint8_t pipeline, uint32_t slot) -> SortKey {
    SortKey key = static_cast<uint64_t>(static_cast<uint16_t>(static_cast<int32_t>(depth.plane.value) + 32768)) << 45;
    if (depth.y_sort) {
        auto y = static_cast<int64_t>(y_relative * 4.0F) + (1LL << 19);
        key |= static_cast<uint64_t>(std::clamp<int64_t>(y, 0, (1LL << 20) - 1)) << 25;
    }
    key |= static_cast<uint64_t>(pipeline & 0x1FU) << 20;
    key |= slot & 0xFFFFFU;
    return key;
}


struct GpuBuffer {
    wgpu::raii::Buffer buffer;
    uint64_t capacity = 0;
};

struct RenderTarget {
    wgpu::raii::Texture texture;
    wgpu::raii::TextureView view;
    uint32_t width = 0;
    uint32_t height = 0;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
};

struct GpuTexture {
    wgpu::raii::Texture texture;
    wgpu::raii::TextureView view;
    glm::vec2 size{0.0F};
};

struct Targets {
    RenderTarget scene;
    RenderTarget aux;
    RenderTarget occluder;
    RenderTarget light;
    RenderTarget light_one;
    RenderTarget light_pong;
    RenderTarget distortion;
    RenderTarget entities;
    RenderTarget overhead;
    RenderTarget lit;
    std::array<RenderTarget, BLOOM_MIPS> bloom;
    RenderTarget ldr_a, ldr_b;
    RenderTarget composed;
    RenderTarget ui;
    RenderTarget snapshot;
    RenderTarget minimap;
};

struct Samplers {
    wgpu::raii::Sampler linear;
    wgpu::raii::Sampler nearest;
};

struct BindLayouts {
    wgpu::raii::BindGroupLayout frame;
    wgpu::raii::BindGroupLayout material;
    wgpu::raii::BindGroupLayout tile;
    wgpu::raii::BindGroupLayout light;
    wgpu::raii::BindGroupLayout composite;
    wgpu::raii::BindGroupLayout post;
    wgpu::raii::BindGroupLayout transition;
    wgpu::raii::BindGroupLayout compose;
    wgpu::raii::BindGroupLayout particles;
    wgpu::raii::BindGroupLayout particle_draw;
    wgpu::raii::BindGroupLayout blit;
};

struct Pipelines {
    std::array<wgpu::raii::RenderPipeline, BLEND_MODES> sprite;
    std::array<wgpu::raii::RenderPipeline, BLEND_MODES> sprite_fallback;
    wgpu::raii::RenderPipeline sprite_distort;
    wgpu::raii::RenderPipeline background;
    wgpu::raii::RenderPipeline tile;
    wgpu::raii::RenderPipeline tile_mask;
    wgpu::raii::RenderPipeline tile_silhouette;
    wgpu::raii::RenderPipeline sprite_flat;
    wgpu::raii::RenderPipeline occluder;
    wgpu::raii::RenderPipeline light;
    wgpu::raii::RenderPipeline light_solid;
    wgpu::raii::RenderPipeline shadow_geom;
    wgpu::raii::RenderPipeline light_accum;
    wgpu::raii::RenderPipeline blur_light;
    wgpu::raii::RenderPipeline smoke;
    wgpu::raii::RenderPipeline lit;
    wgpu::raii::RenderPipeline composite;
    wgpu::raii::RenderPipeline bloom_threshold, bloom_down, bloom_up;
    wgpu::raii::RenderPipeline blur;
    wgpu::raii::RenderPipeline post;
    wgpu::raii::RenderPipeline transition;
    wgpu::raii::RenderPipeline compose;
    wgpu::raii::RenderPipeline blit;
    wgpu::raii::ComputePipeline particle_emit, particle_sim;
    wgpu::raii::RenderPipeline particle_draw;
    wgpu::raii::RenderPipeline particle_distort;
};

struct MaterialPipeline {
    std::array<wgpu::raii::RenderPipeline, BLEND_MODES> blend;
    bool valid = false;
    bool compiled = false;
};

struct MaterialCache {
    std::unordered_map<uint64_t, MaterialPipeline> by_shader;
};

struct TextureCache {
    std::unordered_map<uint64_t, GpuTexture> by_hash;
    std::unordered_map<uint64_t, wgpu::raii::BindGroup> binds;
    std::vector<wgpu::raii::BindGroup> transient;
    std::unordered_set<uint64_t> decode_failed;
};

struct TextureAtlas {
    wgpu::raii::Texture array;
    wgpu::raii::TextureView view;
    wgpu::raii::BindGroup bind;
    std::unordered_map<uint64_t, uint32_t> layer_of;
    uint32_t layers = 0;
    uint32_t capacity = 0;
    uint32_t texels = TILE_TEXELS;
};

struct TileMeshCache {
    struct ChunkMesh {
        GpuBuffer floor;
        GpuBuffer solid;
        uint32_t floor_count = 0;
        uint32_t solid_count = 0;
    };
    std::unordered_map<int64_t, ChunkMesh> chunks;
    uint16_t tileset_version = 0xFFFF;
};

struct ParticleSystem {
    GpuBuffer particles;
    GpuBuffer emitters;
    GpuBuffer counters;
    GpuBuffer alive;
    GpuBuffer indirect;
    wgpu::raii::BindGroup sim_bind;
    wgpu::raii::BindGroup draw_bind;
    WGPUTextureView bound_atlas = nullptr;
    uint32_t capacity = PARTICLE_CAP;
};

struct DrawItem {
    SortKey key;
    uint32_t instance;
};

struct DrawRun {
    uint32_t first = 0;
    uint32_t count = 0;
    uint64_t texture = 0;
    uint64_t normal_map = 0;
    uint64_t shader = 0;
    uint8_t blend = 0;
    bool nearest = false;
    int16_t plane = 0;
};

struct SlotInfo {
    uint64_t texture = 0;
    uint64_t normal_map = 0;
    uint64_t shader = 0;
    bool nearest = false;
};

struct ShadowedLight {
    GpuLight light;
    uint32_t vert_first = 0;
    uint32_t vert_count = 0;
};

struct FrameScratch {
    std::vector<GpuInstance> instances;
    std::vector<DrawItem> items;
    std::vector<DrawRun> runs;
    std::vector<SlotInfo> slots;
    std::vector<GpuLight> lights;
    std::vector<ShadowedLight> shadowed;
    std::vector<glm::vec2> shadow_verts;
    std::vector<GpuLight> smoke;
    std::vector<GpuOccluder> occluders;
    std::vector<GpuEmitter> emitters;
    std::vector<GpuInstance> minimap;

    std::unordered_map<uint64_t, uint64_t> by_nid;
    std::vector<GpuLight> light_upload;
    std::vector<GpuLight> shadow_upload;

    void clear() {
        instances.clear();
        items.clear();
        runs.clear();
        slots.clear();
        lights.clear();
        shadowed.clear();
        shadow_verts.clear();
        smoke.clear();
        occluders.clear();
        emitters.clear();
        minimap.clear();
        by_nid.clear();
        light_upload.clear();
        shadow_upload.clear();
    }
};

struct FrameParams {
    GpuComposite composite{};
    GpuPost post{};
    float blur = 0.0F;
    bool vision = false;
    bool vision_solid = false;
    float dt = 1.0F / 60.0F;
    size_t runs_floor = 0;
    size_t runs_entities = 0;
    size_t runs_overhead = 0;
    glm::vec2 minimap_center{0.0F};
    float minimap_range = 1700.0F;
    bool minimap_active = false;
};

struct Viewport {
    glm::vec2 position{0};
    float zoom = 1.0F;
    float rotation = 0.0F;
    glm::vec2 offset{0};
    glm::vec2 shakeOffset{0};

    [[nodiscard]] auto worldToScreen(const glm::vec2& world, int windowW, int windowH) const -> glm::vec2 {
        glm::vec2 d = (world - position) * zoom;
        if (rotation != 0.0F) {
            float c = std::cos(rotation);
            float s = std::sin(rotation);
            d = {(d.x * c) - (d.y * s), (d.x * s) + (d.y * c)};
        }
        return {d.x + (static_cast<float>(windowW) / 2.0F) + offset.x + shakeOffset.x, d.y + (static_cast<float>(windowH) / 2.0F) + offset.y + shakeOffset.y};
    }

    [[nodiscard]] auto screenToWorld(const glm::vec2& screen, int windowW, int windowH) const -> glm::vec2 {
        glm::vec2 d = {screen.x - (static_cast<float>(windowW) / 2.0F) - offset.x - shakeOffset.x, screen.y - (static_cast<float>(windowH) / 2.0F) - offset.y - shakeOffset.y};
        if (rotation != 0.0F) {
            float c = std::cos(-rotation);
            float s = std::sin(-rotation);
            d = {(d.x * c) - (d.y * s), (d.x * s) + (d.y * c)};
        }
        return (d / zoom) + position;
    }
};

struct Quality {
    float internal_scale = 1.0F;
    float light_scale = 1.0F;
    uint32_t particle_cap = PARTICLE_CAP;
    uint32_t max_lights = 256;
    uint32_t max_shadow_lights = 32;
    uint32_t shadow_taps = 3;
    bool bloom = true;
};

struct TransitionState {
    ScreenTransitionKind kind = ScreenTransitionKind::Fade;
    double start = -1.0;
    float duration = 0.25F;
    glm::vec4 color{0.0F, 0.0F, 0.0F, 1.0F};
    glm::vec2 center{0.5F, 0.5F};
    uint8_t direction = 0;
    TransitionScope scope = TransitionScope::Frame;
    SlideMode slide = SlideMode::Push;
    bool capture_pending = false;
};

struct FrameCtx {
    wgpu::raii::CommandEncoder encoder;
    WGPUSurfaceTexture surface{};
    wgpu::raii::TextureView backbuffer;
    bool ok = false;
};

struct PassBinds {
    wgpu::raii::BindGroup frame;
    wgpu::raii::BindGroup frame_occluder;
    wgpu::raii::BindGroup frame_minimap;
    wgpu::raii::BindGroup light;
    wgpu::raii::BindGroup lit;
    wgpu::raii::BindGroup composite;
    wgpu::raii::BindGroup lit_src;
    std::array<wgpu::raii::BindGroup, BLOOM_MIPS> bloom_src;
    wgpu::raii::BindGroup post_a, post_b;
    wgpu::raii::BindGroup blur_a, blur_b;
    wgpu::raii::BindGroup transition;
    wgpu::raii::BindGroup compose;
    wgpu::raii::BindGroup light_one_src;
    wgpu::raii::BindGroup light_pong_src;
};

struct EmitterState {
    float accumulator = 0.0F;
    uint16_t pending_burst = 0;
};

struct RenderQueries {
    flecs::query<const Sprite> sprites;
    flecs::query<const Position, const Light> lights;
    flecs::query<const Position, const Occluder> occluders;
    flecs::query<const Position, const VisionBlocker> smoke;
    flecs::query<const Position, ParticleEmitter, EmitterState> emitters;
    flecs::query<const NetworkId> nids;
    flecs::query<const Environment> environment;
    flecs::query<const Position> local_view;
    flecs::query<const Position, const RadarVisible> radar;
};

struct RenderState {
    SDL_Window* window = nullptr;
    float dpi = 1.0F;
    bool vsync = true;

    wgpu::raii::Instance instance;
    wgpu::raii::Surface surface;
    wgpu::raii::Adapter adapter;
    wgpu::raii::Device device;
    wgpu::raii::Queue queue;
    wgpu::TextureFormat surface_format = wgpu::TextureFormat::BGRA8Unorm;

    Quality quality;
    Samplers samplers;
    BindLayouts layouts;
    Pipelines pipelines;
    Targets targets;

    GpuBuffer frame_ubo;
    GpuBuffer occluder_ubo;
    GpuBuffer minimap_ubo;
    GpuBuffer composite_ubo;
    GpuBuffer post_ubo;
    GpuBuffer transition_ubo;

    GpuBuffer sprite_instances;
    GpuBuffer minimap_instances;
    GpuBuffer light_instances;
    GpuBuffer shadow_light_instances;
    GpuBuffer shadow_verts;
    GpuBuffer occluder_instances;

    TextureCache textures;
    MaterialCache materials;
    TextureAtlas tile_atlas;
    TextureAtlas particle_atlas;
    TileMeshCache tiles;
    ParticleSystem particles;
    FrameScratch scratch;
    FrameParams params;
    PassBinds binds;
    RenderQueries queries;
    std::vector<GpuEmitter> pending_bursts;
    std::vector<GpuEmitter> emitter_upload;

    Viewport camera;
    TransitionState transition;
    FrameCtx frame;

    Clay_WebGPU_Context clay = nullptr;
    Clay_WebGPU_Font clay_fonts[2] = {};
    Clay_WebGPU_MeasureData clay_measure{};
    Clay_WebGPU_Image minimap_clay = {};

    float shake_time = 0.0F;
    uint64_t frame_index = 0;
    double time = 0.0;
};

struct RenderPipeline {
    flecs::entity_t value = 0;
};

struct Render {
    Render(flecs::world& world);

   private:
    static void init(flecs::iter& it, size_t i);

    static void camera(flecs::iter& it, size_t i, RenderState& render, const Position& pos);

    static void begin(flecs::iter& it);
    static void collect(flecs::iter& it);
    static void compute(flecs::iter& it);
    static void pass_occluder(flecs::iter& it);
    static void pass_world(flecs::iter& it);
    static void pass_distortion(flecs::iter& it);
    static void pass_light(flecs::iter& it);
    static void pass_composite(flecs::iter& it);
    static void pass_post(flecs::iter& it);
    static void pass_minimap(flecs::iter& it);
    static void interface(flecs::iter& it, size_t i, RenderState& render, InterfaceCommands& commands);
    static void pass_transition(flecs::iter& it);
    static void present(flecs::iter& it);

    static void emit(flecs::entity e, const RequestParticles& req);
    static void transition(flecs::entity e, const RequestTransition& req);

    static auto grow_buffer(RenderState& r, GpuBuffer& buf, uint64_t size, WGPUBufferUsage usage) -> WGPUBuffer;
    static void write_buffer(RenderState& r, GpuBuffer& buf, const void* data, uint64_t size, WGPUBufferUsage usage);
    static auto texture(flecs::world world, RenderState& r, uint64_t hash) -> GpuTexture*;
    static auto bind_material(flecs::world world, RenderState& r, uint64_t albedo, uint64_t normal, bool nearest) -> WGPUBindGroup;
    static auto atlas_layer(flecs::world world, RenderState& r, TextureAtlas& atlas, uint64_t hash, bool* pending = nullptr) -> uint32_t;
    static auto material_pipeline(flecs::world world, RenderState& r, uint64_t shader, uint8_t blend) -> WGPURenderPipeline;
    static void resize_targets(RenderState& r, uint32_t width, uint32_t height);
    static void resize_minimap(RenderState& r, uint32_t pixels);
    static auto camera_uniform(const RenderState& r, glm::vec2 center, glm::vec2 extent_px, float zoom, float rotation, glm::vec2 shake) -> GpuCamera;

    static void draw_runs(flecs::world world, RenderState& r, WGPURenderPassEncoder pass, size_t first_run, size_t last_run, bool distortion);
    static void mesh_chunks(flecs::world world, RenderState& r);

    static void particles_setup(RenderState& r);
    static void particles_rebind(RenderState& r);
    static void particles_upload(RenderState& r, float dt);
    static void particles_simulate(RenderState& r, WGPUCommandEncoder encoder);
    static void particles_draw(RenderState& r, WGPURenderPassEncoder pass, bool distortion);

    static void clay_setup(flecs::world world, RenderState& r);
};
