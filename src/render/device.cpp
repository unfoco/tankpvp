#define WEBGPU_CPP_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION

#include "render.h"

#include <sdl3webgpu.h>

#include <stb/stb_image.h>

#include <cstring>
#include <string>

#include "component/event.h"

#include "internal.h"
#include "shader.h"

auto Render::grow_buffer(RenderState& r, GpuBuffer& buf, uint64_t size, WGPUBufferUsage usage) -> WGPUBuffer {
    if (!buf.buffer || buf.capacity < size) {
        uint64_t cap = std::max<uint64_t>(size, 256);
        cap += cap / 2;
        cap = (cap + 255) & ~255ULL;
        WGPUBufferDescriptor d = {};
        d.usage = usage | WGPUBufferUsage_CopyDst;
        d.size = cap;
        buf.buffer = r.device->createBuffer(d);
        buf.capacity = cap;
    }
    return *buf.buffer;
}

void Render::write_buffer(RenderState& r, GpuBuffer& buf, const void* data, uint64_t size, WGPUBufferUsage usage) {
    if (size == 0) {
        return;
    }
    WGPUBuffer handle = grow_buffer(r, buf, size, usage);
    r.queue->writeBuffer(handle, 0, data, size);
}

void Render::init(flecs::iter& it, size_t) {
    flecs::world world = it.world();

    auto* window = SDL_CreateWindow("TankPvP", WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        SDL_Log("render: failed to create window: %s", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    RenderState r;
    r.window = window;
    r.dpi = SDL_GetWindowDisplayScale(window);

    r.instance = wgpu::createInstance(WGPUInstanceDescriptor{});
    r.surface = wgpu::raii::Surface(SDL_GetWGPUSurface(*r.instance, window));
    r.adapter = r.instance->requestAdapter(WGPURequestAdapterOptions{.compatibleSurface = *r.surface});

    WGPUDeviceDescriptor dd = {};
    dd.uncapturedErrorCallbackInfo.callback = [](const WGPUDevice*, WGPUErrorType type, WGPUStringView message, void*, void*) -> void {
        SDL_Log("render: device error (%d): %.*s", static_cast<int>(type), static_cast<int>(message.length), message.data);
    };
    r.device = r.adapter->requestDevice(*r.instance, dd);
    r.queue = r.device->getQueue();

    wgpu::SurfaceCapabilities caps;
    r.surface->getCapabilities(*r.adapter, &caps);
    r.surface_format = wgpu::TextureFormat(WGPUTextureFormat_BGRA8Unorm);
    bool found = false;
    for (size_t i = 0; i < caps.formatCount; ++i) {
        if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
            r.surface_format = wgpu::TextureFormat(caps.formats[i]);
            found = true;
            break;
        }
    }
    if (!found && caps.formatCount > 0) {
        r.surface_format = wgpu::TextureFormat(caps.formats[0]);
    }

    int pw = 0;
    int ph = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    r.surface->configure(WGPUSurfaceConfiguration{
        .device = *r.device,
        .format = r.surface_format,
        .usage = WGPUTextureUsage_RenderAttachment,
        .width = static_cast<uint32_t>(pw),
        .height = static_cast<uint32_t>(ph),
        .alphaMode = WGPUCompositeAlphaMode_Auto,
        .presentMode = WGPUPresentMode_Fifo,
    });

    {
        WGPUSamplerDescriptor sd = {};
        sd.magFilter = WGPUFilterMode_Linear;
        sd.minFilter = WGPUFilterMode_Linear;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_Repeat;
        sd.lodMaxClamp = 32.0F;
        sd.maxAnisotropy = 1;
        r.samplers.linear = r.device->createSampler(sd);
        sd.magFilter = WGPUFilterMode_Nearest;
        sd.minFilter = WGPUFilterMode_Nearest;
        r.samplers.nearest = r.device->createSampler(sd);
    }

    auto texture_entry = [](uint32_t binding, WGPUTextureViewDimension dim = WGPUTextureViewDimension_2D) -> WGPUBindGroupLayoutEntry {
        WGPUBindGroupLayoutEntry e = {};
        e.binding = binding;
        e.visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;
        e.texture.sampleType = WGPUTextureSampleType_Float;
        e.texture.viewDimension = dim;
        return e;
    };
    auto sampler_entry = [](uint32_t binding) -> WGPUBindGroupLayoutEntry {
        WGPUBindGroupLayoutEntry e = {};
        e.binding = binding;
        e.visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;
        e.sampler.type = WGPUSamplerBindingType_Filtering;
        return e;
    };
    auto uniform_entry = [](uint32_t binding, uint64_t size) -> WGPUBindGroupLayoutEntry {
        WGPUBindGroupLayoutEntry e = {};
        e.binding = binding;
        e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform;
        e.buffer.minBindingSize = size;
        return e;
    };
    auto storage_entry = [](uint32_t binding, bool read_only) -> WGPUBindGroupLayoutEntry {
        WGPUBindGroupLayoutEntry e = {};
        e.binding = binding;
        e.visibility = WGPUShaderStage_Compute;
        e.buffer.type = read_only ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
        return e;
    };
    auto make_layout = [&](std::vector<WGPUBindGroupLayoutEntry> entries) -> wgpu::raii::BindGroupLayout {
        WGPUBindGroupLayoutDescriptor d = {};
        d.entryCount = entries.size();
        d.entries = entries.data();
        return r.device->createBindGroupLayout(d);
    };

    r.layouts.frame = make_layout({uniform_entry(0, sizeof(GpuCamera)), texture_entry(1), sampler_entry(2)});
    r.layouts.material = make_layout({texture_entry(0), sampler_entry(1), texture_entry(2)});
    r.layouts.tile = make_layout({texture_entry(0, WGPUTextureViewDimension_2DArray), sampler_entry(1)});
    r.layouts.light = make_layout({texture_entry(0), texture_entry(1), sampler_entry(2), uniform_entry(3, sizeof(GpuCamera))});
    r.layouts.composite = make_layout({texture_entry(0), texture_entry(1), texture_entry(2), texture_entry(3), sampler_entry(4), uniform_entry(5, sizeof(GpuComposite)), texture_entry(6)});
    r.layouts.post = make_layout({texture_entry(0), texture_entry(1), sampler_entry(2), uniform_entry(3, sizeof(GpuPost))});
    r.layouts.transition = make_layout({texture_entry(0), texture_entry(1), texture_entry(2), sampler_entry(3), uniform_entry(4, sizeof(GpuTransition))});
    r.layouts.compose = make_layout({texture_entry(0), texture_entry(1), sampler_entry(2)});
    r.layouts.blit = make_layout({texture_entry(0), sampler_entry(1)});
    {
        auto p0 = storage_entry(0, false);
        auto p1 = storage_entry(1, false);
        auto p2 = storage_entry(2, true);
        auto p3 = storage_entry(3, false);
        auto p4 = texture_entry(4);
        p4.visibility = WGPUShaderStage_Compute;
        auto p5 = uniform_entry(5, sizeof(GpuCamera));
        p5.visibility = WGPUShaderStage_Compute;
        auto p6 = storage_entry(6, false);
        r.layouts.particles = make_layout({p0, p1, p2, p3, p4, p5, p6});
    }
    {
        auto d2 = storage_entry(2, true);
        d2.visibility = WGPUShaderStage_Vertex;
        auto d3 = storage_entry(3, true);
        d3.visibility = WGPUShaderStage_Vertex;
        r.layouts.particle_draw = make_layout({texture_entry(0, WGPUTextureViewDimension_2DArray), sampler_entry(1), d2, d3});
    }

    auto make_ubo = [&](GpuBuffer& buf, uint64_t size) -> void {
        WGPUBufferDescriptor d = {};
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        d.size = size;
        buf.buffer = r.device->createBuffer(d);
        buf.capacity = size;
    };
    make_ubo(r.frame_ubo, sizeof(GpuCamera));
    make_ubo(r.occluder_ubo, sizeof(GpuCamera));
    make_ubo(r.minimap_ubo, sizeof(GpuCamera));
    make_ubo(r.composite_ubo, sizeof(GpuComposite));
    make_ubo(r.post_ubo, sizeof(GpuPost));
    make_ubo(r.transition_ubo, sizeof(GpuTransition));

    r.textures.by_hash[builtin::WHITE] = make_builtin(r, builtin::WHITE);
    r.textures.by_hash[builtin::DISC] = make_builtin(r, builtin::DISC);
    r.textures.by_hash[builtin::NOISE] = make_builtin(r, builtin::NOISE);
    atlas_create(r, r.tile_atlas, TILE_TEXELS, ATLAS_START_LAYERS);
    atlas_create(r, r.particle_atlas, PARTICLE_TEXELS, ATLAS_START_LAYERS);

    std::string frame = shader::FRAME;
    wgpu::raii::ShaderModule sprite_mod = make_module(r, frame + shader::SPRITE_COMMON + shader::SPRITE_BUILTIN_MATERIAL + shader::SPRITE_FRAG, "sprite");
    wgpu::raii::ShaderModule fallback_mod = make_module(r, frame + shader::SPRITE_COMMON + shader::SPRITE_FALLBACK_MATERIAL + shader::SPRITE_FRAG, "fallback");
    wgpu::raii::ShaderModule background_mod = make_module(r, frame + shader::BACKGROUND, "background");
    wgpu::raii::ShaderModule tile_mod = make_module(r, frame + shader::TILE, "tile");
    wgpu::raii::ShaderModule occluder_mod = make_module(r, frame + shader::OCCLUDER, "occluder");
    wgpu::raii::ShaderModule light_mod = make_module(r, frame + shader::LIGHT, "light");
    wgpu::raii::ShaderModule composite_mod = make_module(r, frame + shader::FULLSCREEN_VS + shader::COMPOSITE, "composite");
    wgpu::raii::ShaderModule bloom_mod = make_module(r, frame + shader::FULLSCREEN_VS + shader::BLOOM, "bloom");
    wgpu::raii::ShaderModule post_mod = make_module(r, frame + shader::FULLSCREEN_VS + shader::POST, "post");
    wgpu::raii::ShaderModule transition_mod = make_module(r, frame + shader::FULLSCREEN_VS + shader::TRANSITION, "transition");
    std::string fullscreen_vs = shader::FULLSCREEN_STANDALONE;
    wgpu::raii::ShaderModule blit_mod = make_module(r, fullscreen_vs + shader::BLIT, "blit");
    wgpu::raii::ShaderModule compose_mod = make_module(r, fullscreen_vs + shader::COMPOSE, "compose");
    wgpu::raii::ShaderModule pcompute_mod = make_module(r, shader::PARTICLES_COMPUTE, "particles_compute");
    wgpu::raii::ShaderModule pdraw_mod = make_module(r, frame + shader::PARTICLES_DRAW, "particles_draw");

    auto scene_targets = [&](WGPUBlendState* scene_blend, WGPUBlendState* aux_blend) -> std::vector<WGPUColorTargetState> {
        return {
            {.format = static_cast<WGPUTextureFormat>(FORMAT_SCENE), .blend = scene_blend, .writeMask = WGPUColorWriteMask_All},
            {.format = static_cast<WGPUTextureFormat>(FORMAT_AUX), .blend = aux_blend, .writeMask = WGPUColorWriteMask_All},
        };
    };

    WGPUBlendState aux_blend = straight_blend();
    for (uint32_t b = 0; b < BLEND_MODES; ++b) {
        WGPUBlendState scene_blend = blend_for(static_cast<BlendMode>(b));
        PipelineSpec spec;
        spec.module = *sprite_mod;
        spec.buffers = {sprite_layout()};
        spec.targets = scene_targets(&scene_blend, &aux_blend);
        spec.groups = {*r.layouts.frame, *r.layouts.material};
        r.pipelines.sprite[b] = make_pipeline(r, spec, "sprite");
        spec.module = *fallback_mod;
        r.pipelines.sprite_fallback[b] = make_pipeline(r, spec, "sprite_fallback");
    }
    {
        WGPUBlendState add = additive_blend();
        PipelineSpec spec;
        spec.module = *sprite_mod;
        spec.fs = "fs_distort";
        spec.buffers = {sprite_layout()};
        spec.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_DISTORTION), .blend = &add, .writeMask = WGPUColorWriteMask_All}};
        spec.groups = {*r.layouts.frame, *r.layouts.material};
        r.pipelines.sprite_distort = make_pipeline(r, spec, "sprite_distort");
    }
    {
        PipelineSpec spec;
        spec.module = *background_mod;
        spec.buffers = {sprite_layout()};
        spec.targets = scene_targets(nullptr, nullptr);
        spec.groups = {*r.layouts.frame, *r.layouts.material};
        r.pipelines.background = make_pipeline(r, spec, "background");
    }
    {
        WGPUBlendState normal = blend_for(BlendMode::Normal);
        PipelineSpec spec;
        spec.module = *tile_mod;
        spec.buffers = {tile_layout()};
        spec.targets = scene_targets(&normal, &aux_blend);
        spec.groups = {*r.layouts.frame, *r.layouts.tile};
        r.pipelines.tile = make_pipeline(r, spec, "tile");

        WGPUBlendState max = max_blend();
        PipelineSpec mask;
        mask.module = *tile_mod;
        mask.fs = "fs_mask";
        mask.buffers = {tile_layout()};
        mask.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_OCCLUDER), .blend = &max, .writeMask = WGPUColorWriteMask_All}};
        mask.groups = {*r.layouts.frame};
        r.pipelines.tile_mask = make_pipeline(r, mask, "tile_mask");

        WGPUBlendState premult = blend_for(BlendMode::Normal);
        PipelineSpec flat;
        flat.module = *tile_mod;
        flat.fs = "fs_silhouette";
        flat.buffers = {tile_layout()};
        flat.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_LDR), .blend = &premult, .writeMask = WGPUColorWriteMask_All}};
        flat.groups = {*r.layouts.frame, *r.layouts.tile};
        r.pipelines.tile_silhouette = make_pipeline(r, flat, "tile_silhouette");

        PipelineSpec sflat;
        sflat.module = *sprite_mod;
        sflat.fs = "fs_flat";
        sflat.buffers = {sprite_layout()};
        sflat.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_LDR), .blend = &premult, .writeMask = WGPUColorWriteMask_All}};
        sflat.groups = {*r.layouts.frame, *r.layouts.material};
        r.pipelines.sprite_flat = make_pipeline(r, sflat, "sprite_flat");
    }
    {
        WGPUBlendState max = max_blend();
        PipelineSpec spec;
        spec.module = *occluder_mod;
        spec.buffers = {occluder_layout()};
        spec.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_OCCLUDER), .blend = &max, .writeMask = WGPUColorWriteMask_All}};
        spec.groups = {*r.layouts.frame};
        r.pipelines.occluder = make_pipeline(r, spec, "occluder");
    }
    {
        WGPUBlendState add = additive_blend();
        PipelineSpec spec;
        spec.module = *light_mod;
        spec.fs = "fs_light";
        spec.buffers = {light_layout()};
        spec.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_LIGHT), .blend = &add, .writeMask = WGPUColorWriteMask_All}};
        spec.groups = {*r.layouts.frame, *r.layouts.light};
        r.pipelines.light = make_pipeline(r, spec, "light");

        spec.fs = "fs_light_solid";
        spec.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_LIGHT), .blend = nullptr, .writeMask = WGPUColorWriteMask_All}};
        r.pipelines.light_solid = make_pipeline(r, spec, "light_solid");

        WGPUBlendState sub = smoke_blend();
        spec.fs = "fs_smoke";
        spec.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_LIGHT), .blend = &sub, .writeMask = WGPUColorWriteMask_All}};
        r.pipelines.smoke = make_pipeline(r, spec, "smoke");
    }
    {
        wgpu::raii::ShaderModule shadow_mod = make_module(r, frame + shader::SHADOW_GEOM, "shadow_geom");
        static const std::vector<WGPUVertexAttribute> attrs = {{.format = WGPUVertexFormat_Float32x2, .offset = 0, .shaderLocation = 0}};
        PipelineSpec spec;
        spec.module = *shadow_mod;
        spec.buffers = {{.stepMode = WGPUVertexStepMode_Vertex, .arrayStride = sizeof(glm::vec2), .attributeCount = attrs.size(), .attributes = attrs.data()}};
        spec.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_LIGHT), .blend = nullptr, .writeMask = WGPUColorWriteMask_All}};
        spec.groups = {*r.layouts.frame};
        r.pipelines.shadow_geom = make_pipeline(r, spec, "shadow_geom");
    }
    auto fullscreen = [&](WGPUShaderModule mod, const char* fs, wgpu::TextureFormat fmt, WGPUBindGroupLayout group1, const char* label) -> wgpu::raii::RenderPipeline {
        PipelineSpec spec;
        spec.module = mod;
        spec.vs = "vs_fullscreen";
        spec.fs = fs;
        spec.targets = {{.format = static_cast<WGPUTextureFormat>(fmt), .blend = nullptr, .writeMask = WGPUColorWriteMask_All}};
        spec.groups = {*r.layouts.frame, group1};
        return make_pipeline(r, spec, label);
    };
    r.pipelines.lit = fullscreen(*composite_mod, "fs_lit", FORMAT_SCENE, *r.layouts.composite, "lit");
    r.pipelines.composite = fullscreen(*composite_mod, "fs_composite", FORMAT_LDR, *r.layouts.composite, "composite");
    r.pipelines.bloom_threshold = fullscreen(*bloom_mod, "fs_threshold", FORMAT_SCENE, *r.layouts.blit, "bloom_threshold");
    r.pipelines.bloom_down = fullscreen(*bloom_mod, "fs_down", FORMAT_SCENE, *r.layouts.blit, "bloom_down");
    r.pipelines.bloom_up = fullscreen(*bloom_mod, "fs_up", FORMAT_SCENE, *r.layouts.blit, "bloom_up");
    r.pipelines.blur = fullscreen(*bloom_mod, "fs_blur", FORMAT_LDR, *r.layouts.blit, "blur");
    r.pipelines.blur_light = fullscreen(*bloom_mod, "fs_blur", FORMAT_LIGHT, *r.layouts.blit, "blur_light");
    r.pipelines.post = fullscreen(*post_mod, "fs_main", FORMAT_LDR, *r.layouts.post, "post");
    r.pipelines.transition = fullscreen(*transition_mod, "fs_main", r.surface_format, *r.layouts.transition, "transition");
    {
        PipelineSpec spec;
        spec.module = *blit_mod;
        spec.vs = "vs_fullscreen";
        spec.targets = {{.format = static_cast<WGPUTextureFormat>(r.surface_format), .blend = nullptr, .writeMask = WGPUColorWriteMask_All}};
        spec.groups = {*r.layouts.blit};
        r.pipelines.blit = make_pipeline(r, spec, "blit");

        PipelineSpec comp;
        comp.module = *compose_mod;
        comp.vs = "vs_fullscreen";
        comp.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_LDR), .blend = nullptr, .writeMask = WGPUColorWriteMask_All}};
        comp.groups = {*r.layouts.compose};
        r.pipelines.compose = make_pipeline(r, comp, "compose");

        WGPUBlendState add = additive_blend();
        spec.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_LIGHT), .blend = &add, .writeMask = WGPUColorWriteMask_All}};
        r.pipelines.light_accum = make_pipeline(r, spec, "light_accum");
    }
    {
        WGPUComputePipelineDescriptor cd = {};
        WGPUPipelineLayoutDescriptor ld = {};
        WGPUBindGroupLayout groups[1] = {*r.layouts.particles};
        ld.bindGroupLayoutCount = 1;
        ld.bindGroupLayouts = groups;
        wgpu::raii::PipelineLayout layout = r.device->createPipelineLayout(ld);
        cd.layout = *layout;
        cd.compute.module = *pcompute_mod;
        cd.compute.entryPoint = str("cs_emit");
        r.pipelines.particle_emit = r.device->createComputePipeline(cd);
        cd.compute.entryPoint = str("cs_sim");
        r.pipelines.particle_sim = r.device->createComputePipeline(cd);
    }
    {
        WGPUBlendState premult = blend_for(BlendMode::Normal);
        PipelineSpec spec;
        spec.module = *pdraw_mod;
        spec.targets = scene_targets(&premult, &aux_blend);
        spec.groups = {*r.layouts.frame, *r.layouts.particle_draw};
        r.pipelines.particle_draw = make_pipeline(r, spec, "particle_draw");

        WGPUBlendState add = additive_blend();
        spec.fs = "fs_distort";
        spec.targets = {{.format = static_cast<WGPUTextureFormat>(FORMAT_DISTORTION), .blend = &add, .writeMask = WGPUColorWriteMask_All}};
        r.pipelines.particle_distort = make_pipeline(r, spec, "particle_distort");
    }

    Render::particles_setup(r);
    Render::resize_targets(r, static_cast<uint32_t>(pw), static_cast<uint32_t>(ph));
    Render::resize_minimap(r, 256);
    Render::clay_setup(world, r);

    r.queries = {
        .sprites = world.query_builder<const Sprite>().build(),
        .lights = world.query_builder<const Position, const Light>().build(),
        .occluders = world.query_builder<const Position, const Occluder>().build(),
        .smoke = world.query_builder<const Position, const VisionBlocker>().build(),
        .emitters = world.query_builder<const Position, ParticleEmitter, EmitterState>().build(),
        .nids = world.query_builder<const NetworkId>().build(),
        .environment = world.query_builder<const Environment>().build(),
        .local_view = world.query_builder<const Position>().with<Local>().build(),
        .radar = world.query_builder<const Position, const RadarVisible>().without<Dying>().without<Local>().build(),
    };

    world.set<WindowEvents>({.target = window});

    world.defer_suspend();
    world.set<RenderState>(std::move(r));
    RenderState& live = world.get_mut<RenderState>();
    live.clay_measure = {.ctx = live.clay, .fonts = live.clay_fonts};
    Clay_SetMeasureTextFunction(Clay_WebGPU_MeasureText, &live.clay_measure);
    world.set<MinimapHandle>({.image = &live.minimap_clay});
    world.defer_resume();
}
