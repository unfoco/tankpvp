
#define WEBGPU_CPP_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION

#include "render.h"

#include <sdl3webgpu.h>

#include <stb/stb_image.h>

#include <cstring>
#include <string>

#include "component/asset.h"
#include "component/event.h"

#include "shader.h"

namespace {

constexpr uint32_t ATLAS_START_LAYERS = 64;

auto str(const char* s) -> WGPUStringView {
    return {s, WGPU_STRLEN};
}

auto blend_for(BlendMode mode) -> WGPUBlendState {
    auto make = [](WGPUBlendOperation op, WGPUBlendFactor src, WGPUBlendFactor dst, WGPUBlendOperation aop, WGPUBlendFactor asrc, WGPUBlendFactor adst) -> WGPUBlendState {
        return {.color = {.operation = op, .srcFactor = src, .dstFactor = dst}, .alpha = {.operation = aop, .srcFactor = asrc, .dstFactor = adst}};
    };
    switch (mode) {
        case BlendMode::Add:
            return make(WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One, WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One);
        case BlendMode::Multiply:
            return make(WGPUBlendOperation_Add, WGPUBlendFactor_Dst, WGPUBlendFactor_Zero, WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One);
        case BlendMode::Modulate:
            return make(WGPUBlendOperation_Add, WGPUBlendFactor_Dst, WGPUBlendFactor_OneMinusSrcAlpha, WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One);
        case BlendMode::Screen:
            return make(WGPUBlendOperation_Add, WGPUBlendFactor_OneMinusDst, WGPUBlendFactor_One, WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One);
        case BlendMode::Subtract:
            return make(WGPUBlendOperation_ReverseSubtract, WGPUBlendFactor_One, WGPUBlendFactor_One, WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One);
        case BlendMode::Normal:
        default:
            return make(WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha, WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha);
    }
}

auto additive_blend() -> WGPUBlendState {
    return {.color = {.operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_One},
            .alpha = {.operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_One}};
}

auto max_blend() -> WGPUBlendState {
    return {.color = {.operation = WGPUBlendOperation_Max, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_One},
            .alpha = {.operation = WGPUBlendOperation_Max, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_One}};
}

auto straight_blend() -> WGPUBlendState {
    return {.color = {.operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_SrcAlpha, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha},
            .alpha = {.operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha}};
}

auto smoke_blend() -> WGPUBlendState {
    return {.color = {.operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_Zero, .dstFactor = WGPUBlendFactor_One},
            .alpha = {.operation = WGPUBlendOperation_ReverseSubtract, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_One}};
}

auto make_module(RenderState& r, const std::string& source, const char* label) -> wgpu::raii::ShaderModule {
    WGPUShaderSourceWGSL wgsl = {};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = {source.c_str(), source.size()};
    WGPUShaderModuleDescriptor desc = {};
    desc.nextInChain = &wgsl.chain;
    desc.label = str(label);
    return wgpu::raii::ShaderModule(wgpu::ShaderModule(wgpuDeviceCreateShaderModule(*r.device, &desc)));
}

struct PipelineSpec {
    WGPUShaderModule module = nullptr;
    const char* vs = "vs_main";
    const char* fs = "fs_main";
    std::vector<WGPUVertexBufferLayout> buffers;
    std::vector<WGPUColorTargetState> targets;
    std::vector<WGPUBindGroupLayout> groups;
    WGPUShaderModule fs_module = nullptr;
};

auto make_pipeline(RenderState& r, const PipelineSpec& spec, const char* label) -> wgpu::raii::RenderPipeline {
    WGPUPipelineLayoutDescriptor ld = {};
    ld.bindGroupLayoutCount = spec.groups.size();
    ld.bindGroupLayouts = spec.groups.data();
    wgpu::raii::PipelineLayout layout = r.device->createPipelineLayout(ld);

    WGPUFragmentState fs = {};
    fs.module = spec.fs_module != nullptr ? spec.fs_module : spec.module;
    fs.entryPoint = str(spec.fs);
    fs.targetCount = spec.targets.size();
    fs.targets = spec.targets.data();

    WGPURenderPipelineDescriptor d = {};
    d.label = str(label);
    d.layout = *layout;
    d.vertex.module = spec.module;
    d.vertex.entryPoint = str(spec.vs);
    d.vertex.bufferCount = spec.buffers.size();
    d.vertex.buffers = spec.buffers.data();
    d.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    d.primitive.frontFace = WGPUFrontFace_CCW;
    d.primitive.cullMode = WGPUCullMode_None;
    d.multisample.count = 1;
    d.multisample.mask = ~0U;
    d.fragment = &fs;
    return r.device->createRenderPipeline(d);
}


auto sprite_attributes() -> const std::vector<WGPUVertexAttribute>& {
    static const std::vector<WGPUVertexAttribute> attrs = {
        {.format = WGPUVertexFormat_Float32x4, .offset = 0, .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x4, .offset = 16, .shaderLocation = 1},
        {.format = WGPUVertexFormat_Float32x4, .offset = 32, .shaderLocation = 2},
        {.format = WGPUVertexFormat_Float32x4, .offset = 48, .shaderLocation = 3},
        {.format = WGPUVertexFormat_Float32x4, .offset = 64, .shaderLocation = 4},
        {.format = WGPUVertexFormat_Uint32x4, .offset = 80, .shaderLocation = 5},
        {.format = WGPUVertexFormat_Float32x4, .offset = 96, .shaderLocation = 6},
        {.format = WGPUVertexFormat_Float32x4, .offset = 112, .shaderLocation = 7},
    };
    return attrs;
}

auto sprite_layout() -> WGPUVertexBufferLayout {
    const auto& attrs = sprite_attributes();
    return {.stepMode = WGPUVertexStepMode_Instance, .arrayStride = sizeof(GpuInstance), .attributeCount = attrs.size(), .attributes = attrs.data()};
}

auto tile_layout() -> WGPUVertexBufferLayout {
    static const std::vector<WGPUVertexAttribute> attrs = {
        {.format = WGPUVertexFormat_Float32x2, .offset = 0, .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x4, .offset = 8, .shaderLocation = 1},
        {.format = WGPUVertexFormat_Uint32x2, .offset = 24, .shaderLocation = 2},
    };
    return {.stepMode = WGPUVertexStepMode_Instance, .arrayStride = sizeof(GpuTile), .attributeCount = attrs.size(), .attributes = attrs.data()};
}

auto light_layout() -> WGPUVertexBufferLayout {
    static const std::vector<WGPUVertexAttribute> attrs = {
        {.format = WGPUVertexFormat_Float32x4, .offset = 0, .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x4, .offset = 16, .shaderLocation = 1},
        {.format = WGPUVertexFormat_Float32x4, .offset = 32, .shaderLocation = 2},
    };
    return {.stepMode = WGPUVertexStepMode_Instance, .arrayStride = sizeof(GpuLight), .attributeCount = attrs.size(), .attributes = attrs.data()};
}

auto occluder_layout() -> WGPUVertexBufferLayout {
    static const std::vector<WGPUVertexAttribute> attrs = {
        {.format = WGPUVertexFormat_Float32x4, .offset = 0, .shaderLocation = 0},
        {.format = WGPUVertexFormat_Float32x4, .offset = 16, .shaderLocation = 1},
    };
    return {.stepMode = WGPUVertexStepMode_Instance, .arrayStride = sizeof(GpuOccluder), .attributeCount = attrs.size(), .attributes = attrs.data()};
}


void make_target(RenderState& r, RenderTarget& t, uint32_t w, uint32_t h, wgpu::TextureFormat format, WGPUTextureUsage extra = 0) {
    w = std::max(w, 1U);
    h = std::max(h, 1U);
    if (t.view && t.width == w && t.height == h && t.format == format) {
        return;
    }
    WGPUTextureDescriptor d = {};
    d.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | extra;
    d.dimension = WGPUTextureDimension_2D;
    d.size = {w, h, 1};
    d.format = format;
    d.mipLevelCount = 1;
    d.sampleCount = 1;
    t.texture = r.device->createTexture(d);
    t.view = wgpu::raii::TextureView(wgpuTextureCreateView(*t.texture, nullptr));
    t.width = w;
    t.height = h;
    t.format = format;
}


void upload_rgba(RenderState& r, WGPUTexture tex, const uint8_t* pixels, uint32_t w, uint32_t h, uint32_t layer = 0) {
    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = tex;
    dst.origin = {0, 0, layer};
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout lay = {};
    lay.bytesPerRow = w * 4;
    lay.rowsPerImage = h;
    r.queue->writeTexture(dst, pixels, static_cast<size_t>(w) * h * 4, lay, {w, h, 1});
}

auto make_texture_rgba(RenderState& r, const uint8_t* pixels, uint32_t w, uint32_t h) -> GpuTexture {
    GpuTexture out;
    WGPUTextureDescriptor d = {};
    d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    d.dimension = WGPUTextureDimension_2D;
    d.size = {w, h, 1};
    d.format = WGPUTextureFormat_RGBA8Unorm;
    d.mipLevelCount = 1;
    d.sampleCount = 1;
    out.texture = r.device->createTexture(d);
    out.view = wgpu::raii::TextureView(wgpuTextureCreateView(*out.texture, nullptr));
    out.size = {static_cast<float>(w), static_cast<float>(h)};
    upload_rgba(r, *out.texture, pixels, w, h);
    return out;
}

auto hash01(uint32_t x, uint32_t y, uint32_t s) -> float {
    uint32_t v = (x * 1664525U) ^ (y * 22695477U) ^ (s * 2654435761U);
    v ^= v >> 16;
    v *= 2246822519U;
    v ^= v >> 13;
    return static_cast<float>(v & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

auto gen_builtin_pixels(uint64_t hash, int& w, int& h) -> std::vector<uint8_t> {
    if (hash == builtin::WHITE) {
        w = h = 1;
        return std::vector<uint8_t>(4, 255);
    }
    if (hash == builtin::DISC) {
        constexpr int N = 64;
        std::vector<uint8_t> px(static_cast<size_t>(N) * N * 4, 255);
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                float dx = (static_cast<float>(x) + 0.5F - (N / 2.0F)) / (N / 2.0F);
                float dy = (static_cast<float>(y) + 0.5F - (N / 2.0F)) / (N / 2.0F);
                float fall = std::clamp(1.0F - std::sqrt((dx * dx) + (dy * dy)), 0.0F, 1.0F);
                px[((static_cast<size_t>(y) * N + x) * 4) + 3] = static_cast<uint8_t>(fall * fall * 255.0F);
            }
        }
        w = h = N;
        return px;
    }
    constexpr int N = 256;
    std::vector<uint8_t> px(static_cast<size_t>(N) * N * 4, 255);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float v = 0.0F;
            float amp = 0.55F;
            for (int o = 0; o < 3; ++o) {
                int cell = 8 << o;
                int step = N / cell;
                int cx = x / step;
                int cy = y / step;
                float fx = static_cast<float>(x % step) / static_cast<float>(step);
                float fy = static_cast<float>(y % step) / static_cast<float>(step);
                fx = fx * fx * (3.0F - 2.0F * fx);
                fy = fy * fy * (3.0F - 2.0F * fy);
                float a = hash01(cx % cell, cy % cell, o);
                float b = hash01((cx + 1) % cell, cy % cell, o);
                float c = hash01(cx % cell, (cy + 1) % cell, o);
                float d = hash01((cx + 1) % cell, (cy + 1) % cell, o);
                v += amp * std::lerp(std::lerp(a, b, fx), std::lerp(c, d, fx), fy);
                amp *= 0.5F;
            }
            auto g = static_cast<uint8_t>(std::clamp(v, 0.0F, 1.0F) * 255.0F);
            uint8_t* p = &px[(static_cast<size_t>(y) * N + x) * 4];
            p[0] = p[1] = p[2] = g;
        }
    }
    w = h = N;
    return px;
}

auto make_builtin(RenderState& r, uint64_t hash) -> GpuTexture {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px = gen_builtin_pixels(hash, w, h);
    return make_texture_rgba(r, px.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h));
}

auto decode_asset(flecs::world world, RenderState& r, uint64_t hash, int* w, int* h) -> uint8_t* {
    const auto* store = world.try_get<AssetStore>();
    if (store == nullptr) {
        return nullptr;
    }
    auto it = store->ready.find(hash);
    if (it == store->ready.end()) {
        return nullptr;
    }
    int comp = 0;
    uint8_t* pixels = stbi_load(it->second.c_str(), w, h, &comp, 4);
    if (pixels == nullptr && r.textures.decode_failed.insert(hash).second) {
        SDL_Log("render: FAILED to decode asset %llu at '%s' (%s)", static_cast<unsigned long long>(hash), it->second.c_str(), stbi_failure_reason());
    }
    return pixels;
}

void resample(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            int x0 = x * sw / dw;
            int x1 = std::max((x + 1) * sw / dw, x0 + 1);
            int y0 = y * sh / dh;
            int y1 = std::max((y + 1) * sh / dh, y0 + 1);
            uint32_t acc[4] = {0, 0, 0, 0};
            uint32_t n = 0;
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    const uint8_t* p = &src[((sy * sw) + sx) * 4];
                    for (int k = 0; k < 4; ++k) {
                        acc[k] += p[k];
                    }
                    ++n;
                }
            }
            uint8_t* o = &dst[((y * dw) + x) * 4];
            for (int k = 0; k < 4; ++k) {
                o[k] = static_cast<uint8_t>(acc[k] / std::max(n, 1U));
            }
        }
    }
}

void atlas_create(RenderState& r, TextureAtlas& atlas, uint32_t texels, uint32_t layers) {
    WGPUTextureDescriptor d = {};
    d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    d.dimension = WGPUTextureDimension_2D;
    d.size = {texels, texels, layers};
    d.format = WGPUTextureFormat_RGBA8Unorm;
    d.mipLevelCount = 1;
    d.sampleCount = 1;
    wgpu::raii::Texture next = r.device->createTexture(d);

    if (atlas.array && atlas.layers > 0) {
        SDL_assert(atlas.texels == texels);
        WGPUTexelCopyTextureInfo src = {};
        src.texture = *atlas.array;
        src.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyTextureInfo dst = src;
        dst.texture = *next;
        wgpu::raii::CommandEncoder enc = r.device->createCommandEncoder();
        enc->copyTextureToTexture(src, dst, {texels, texels, atlas.layers});
        auto cmd = enc->finish();
        r.queue->submit(1, &cmd);
        wgpuCommandBufferRelease(cmd);
    }
    atlas.array = next;
    WGPUTextureViewDescriptor vd = {};
    vd.dimension = WGPUTextureViewDimension_2DArray;
    vd.format = WGPUTextureFormat_RGBA8Unorm;
    vd.baseMipLevel = 0;
    vd.mipLevelCount = 1;
    vd.baseArrayLayer = 0;
    vd.arrayLayerCount = layers;
    atlas.view = wgpu::raii::TextureView(wgpuTextureCreateView(*atlas.array, &vd));
    atlas.capacity = layers;
    atlas.texels = texels;

    WGPUBindGroupEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].textureView = *atlas.view;
    entries[1].binding = 1;
    entries[1].sampler = *r.samplers.nearest;
    WGPUBindGroupDescriptor bd = {};
    bd.layout = *r.layouts.tile;
    bd.entryCount = 2;
    bd.entries = entries;
    atlas.bind = r.device->createBindGroup(bd);
}

void atlas_upload_layer(RenderState& r, TextureAtlas& atlas, uint32_t layer, const uint8_t* rgba, int w, int h) {
    std::vector<uint8_t> scaled;
    const uint8_t* data = rgba;
    if (static_cast<uint32_t>(w) != atlas.texels || static_cast<uint32_t>(h) != atlas.texels) {
        scaled.resize(static_cast<size_t>(atlas.texels) * atlas.texels * 4);
        resample(rgba, w, h, scaled.data(), static_cast<int>(atlas.texels), static_cast<int>(atlas.texels));
        data = scaled.data();
    }
    upload_rgba(r, *atlas.array, data, atlas.texels, atlas.texels, layer);
}


struct ScopeResult {
    bool done = false;
    bool failed = false;
};

void compile_material(flecs::world world, RenderState& r, uint64_t shader, MaterialPipeline& out) {
    out.compiled = true;
    out.valid = false;

    const auto* store = world.try_get<AssetStore>();
    if (store == nullptr) {
        return;
    }
    auto it = store->ready.find(shader);
    if (it == store->ready.end()) {
        out.compiled = false;
        return;
    }
    SDL_IOStream* io = SDL_IOFromFile(it->second.c_str(), "rb");
    if (io == nullptr) {
        return;
    }
    size_t len = 0;
    void* data = SDL_LoadFile_IO(io, &len, true);
    if (data == nullptr) {
        return;
    }
    std::string chunk(static_cast<char*>(data), len);
    SDL_free(data);

    std::string source = std::string(shader::FRAME) + shader::SPRITE_COMMON + chunk + shader::SPRITE_FRAG;

    ScopeResult result;
    r.device->pushErrorScope(wgpu::ErrorFilter::Validation);

    wgpu::raii::ShaderModule module = make_module(r, source, "material");
    std::array<wgpu::raii::RenderPipeline, BLEND_MODES> pipelines;
    for (uint32_t b = 0; b < BLEND_MODES; ++b) {
        WGPUBlendState scene_blend = blend_for(static_cast<BlendMode>(b));
        WGPUBlendState aux_blend = straight_blend();
        PipelineSpec spec;
        spec.module = *module;
        spec.buffers = {sprite_layout()};
        spec.targets = {
            {.format = static_cast<WGPUTextureFormat>(FORMAT_SCENE), .blend = &scene_blend, .writeMask = WGPUColorWriteMask_All},
            {.format = static_cast<WGPUTextureFormat>(FORMAT_AUX), .blend = &aux_blend, .writeMask = WGPUColorWriteMask_All},
        };
        spec.groups = {*r.layouts.frame, *r.layouts.material};
        pipelines[b] = make_pipeline(r, spec, "material");
    }

    WGPUPopErrorScopeCallbackInfo cb = {};
    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type, WGPUStringView message, void* ud1, void*) -> void {
        auto* res = static_cast<ScopeResult*>(ud1);
        res->done = true;
        res->failed = type != WGPUErrorType_NoError;
        if (res->failed) {
            SDL_Log("render: custom shader rejected: %.*s", static_cast<int>(message.length), message.data);
        }
    };
    cb.userdata1 = &result;
    r.device->popErrorScope(cb);
    for (int spin = 0; spin < 1000 && !result.done; ++spin) {
        r.instance->processEvents();
        r.device->poll(false, nullptr);
    }

    if (result.done && !result.failed) {
        out.blend = std::move(pipelines);
        out.valid = true;
        SDL_Log("render: custom shader %llu compiled", static_cast<unsigned long long>(shader));
    }
}

}


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

auto Render::texture(flecs::world world, RenderState& r, uint64_t hash) -> GpuTexture* {
    if (hash == 0) {
        return nullptr;
    }
    auto it = r.textures.by_hash.find(hash);
    if (it != r.textures.by_hash.end()) {
        return &it->second;
    }
    if (hash <= builtin::LAST) {
        return &(r.textures.by_hash[hash] = make_builtin(r, hash));
    }
    int w = 0;
    int h = 0;
    uint8_t* pixels = decode_asset(world, r, hash, &w, &h);
    if (pixels == nullptr) {
        return nullptr;
    }
    GpuTexture tex = make_texture_rgba(r, pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    stbi_image_free(pixels);
    return &(r.textures.by_hash[hash] = std::move(tex));
}

auto Render::bind_material(flecs::world world, RenderState& r, uint64_t albedo, uint64_t normal, bool nearest) -> WGPUBindGroup {
    uint64_t key = albedo ^ (normal * 0x9E3779B97F4A7C15ULL) ^ (nearest ? 0x1ULL << 63 : 0);
    auto it = r.textures.binds.find(key);
    if (it != r.textures.binds.end()) {
        return *it->second;
    }
    GpuTexture* a = texture(world, r, albedo);
    bool fallback = false;
    if (a == nullptr) {
        a = texture(world, r, builtin::WHITE);
        fallback = true;
    }
    GpuTexture* n = normal != 0 ? texture(world, r, normal) : nullptr;
    if (n == nullptr) {
        n = texture(world, r, builtin::WHITE);
        if (normal != 0) {
            fallback = true;
        }
    }
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].textureView = *a->view;
    entries[1].binding = 1;
    entries[1].sampler = nearest ? *r.samplers.nearest : *r.samplers.linear;
    entries[2].binding = 2;
    entries[2].textureView = *n->view;
    WGPUBindGroupDescriptor d = {};
    d.layout = *r.layouts.material;
    d.entryCount = 3;
    d.entries = entries;
    wgpu::raii::BindGroup bind = r.device->createBindGroup(d);
    WGPUBindGroup raw = *bind;
    if (!fallback) {
        r.textures.binds.emplace(key, std::move(bind));
    } else {
        r.textures.transient.push_back(std::move(bind));
    }
    return raw;
}

auto Render::atlas_layer(flecs::world world, RenderState& r, TextureAtlas& atlas, uint64_t hash, bool* pending) -> uint32_t {
    if (hash == 0) {
        hash = builtin::WHITE;
    }
    auto it = atlas.layer_of.find(hash);
    if (it != atlas.layer_of.end()) {
        return it->second;
    }
    int w = 0;
    int h = 0;
    uint8_t* pixels = nullptr;
    std::vector<uint8_t> generated;
    if (hash <= builtin::LAST) {
        generated = gen_builtin_pixels(hash, w, h);
        pixels = generated.data();
    } else {
        pixels = decode_asset(world, r, hash, &w, &h);
        if (pixels == nullptr) {
            if (pending != nullptr) {
                *pending = true;
            }
            return 0;
        }
    }

    if (atlas.layers >= atlas.capacity) {
        atlas_create(r, atlas, atlas.texels, atlas.capacity * 2);
    }
    uint32_t layer = atlas.layers++;
    atlas_upload_layer(r, atlas, layer, pixels, w, h);
    if (generated.empty()) {
        stbi_image_free(pixels);
    }
    atlas.layer_of[hash] = layer;
    return layer;
}

auto Render::material_pipeline(flecs::world world, RenderState& r, uint64_t shader, uint8_t blend) -> WGPURenderPipeline {
    blend = std::min<uint8_t>(blend, BLEND_MODES - 1);
    if (shader == 0) {
        return *r.pipelines.sprite[blend];
    }
    auto& entry = r.materials.by_shader[shader];
    if (!entry.compiled) {
        compile_material(world, r, shader, entry);
    }
    if (entry.valid) {
        return *entry.blend[blend];
    }
    return *r.pipelines.sprite_fallback[blend];
}

void Render::resize_targets(RenderState& r, uint32_t width, uint32_t height) {
    auto& t = r.targets;
    float is = std::clamp(r.quality.internal_scale, 0.25F, 1.0F);
    auto iw = static_cast<uint32_t>(static_cast<float>(width) * is);
    auto ih = static_cast<uint32_t>(static_cast<float>(height) * is);

    make_target(r, t.scene, iw, ih, FORMAT_SCENE);
    make_target(r, t.aux, iw, ih, FORMAT_AUX);
    make_target(r, t.distortion, iw, ih, FORMAT_DISTORTION);
    make_target(r, t.entities, iw, ih, FORMAT_SCENE);
    make_target(r, t.overhead, iw, ih, FORMAT_SCENE);
    make_target(r, t.lit, iw, ih, FORMAT_SCENE);

    auto pad = static_cast<uint32_t>(OCCLUDER_PAD * is) * 2;
    make_target(r, t.occluder, iw + pad, ih + pad, FORMAT_OCCLUDER);

    float ls = std::clamp(r.quality.light_scale, 0.125F, 1.0F);
    auto lw = static_cast<uint32_t>(static_cast<float>(iw) * ls);
    auto lh = static_cast<uint32_t>(static_cast<float>(ih) * ls);
    make_target(r, t.light, lw, lh, FORMAT_LIGHT);
    make_target(r, t.light_one, lw, lh, FORMAT_LIGHT);
    make_target(r, t.light_pong, lw, lh, FORMAT_LIGHT);

    uint32_t bw = iw;
    uint32_t bh = ih;
    for (uint32_t i = 0; i < BLOOM_MIPS; ++i) {
        bw = std::max(bw / 2, 1U);
        bh = std::max(bh / 2, 1U);
        make_target(r, t.bloom[i], bw, bh, FORMAT_SCENE);
    }

    make_target(r, t.ldr_a, width, height, FORMAT_LDR);
    make_target(r, t.ldr_b, width, height, FORMAT_LDR);
    make_target(r, t.composed, width, height, FORMAT_LDR);
    make_target(r, t.ui, width, height, FORMAT_LDR, WGPUTextureUsage_CopySrc);
    make_target(r, t.snapshot, width, height, FORMAT_LDR, WGPUTextureUsage_CopyDst);

    auto bind = [&](WGPUBindGroupLayout layout, std::vector<WGPUBindGroupEntry> entries) -> wgpu::raii::BindGroup {
        WGPUBindGroupDescriptor d = {};
        d.layout = layout;
        d.entryCount = entries.size();
        d.entries = entries.data();
        return r.device->createBindGroup(d);
    };
    auto tex = [](uint32_t binding, const RenderTarget& target) -> WGPUBindGroupEntry {
        WGPUBindGroupEntry e = {};
        e.binding = binding;
        e.textureView = *target.view;
        return e;
    };
    auto samp = [&](uint32_t binding) -> WGPUBindGroupEntry {
        WGPUBindGroupEntry e = {};
        e.binding = binding;
        e.sampler = *r.samplers.linear;
        return e;
    };
    auto ubo = [](uint32_t binding, const GpuBuffer& buf, uint64_t size) -> WGPUBindGroupEntry {
        WGPUBindGroupEntry e = {};
        e.binding = binding;
        e.buffer = *buf.buffer;
        e.size = size;
        return e;
    };
    auto view_entry = [](uint32_t binding, WGPUTextureView view) -> WGPUBindGroupEntry {
        WGPUBindGroupEntry e = {};
        e.binding = binding;
        e.textureView = view;
        return e;
    };
    WGPUTextureView noise = *r.textures.by_hash[builtin::NOISE].view;
    WGPUTextureView white = *r.textures.by_hash[builtin::WHITE].view;

    auto& b = r.binds;
    b.frame = bind(*r.layouts.frame, {ubo(0, r.frame_ubo, sizeof(GpuCamera)), view_entry(1, noise), samp(2)});
    b.frame_occluder = bind(*r.layouts.frame, {ubo(0, r.occluder_ubo, sizeof(GpuCamera)), view_entry(1, noise), samp(2)});
    b.frame_minimap = bind(*r.layouts.frame, {ubo(0, r.minimap_ubo, sizeof(GpuCamera)), view_entry(1, noise), samp(2)});
    b.light = bind(*r.layouts.light, {tex(0, t.occluder), tex(1, t.aux), samp(2), ubo(3, r.occluder_ubo, sizeof(GpuCamera))});
    b.lit = bind(*r.layouts.composite, {tex(0, t.scene), tex(1, t.aux), tex(2, t.light), tex(3, t.entities), samp(4), ubo(5, r.composite_ubo, sizeof(GpuComposite)), tex(6, t.overhead)});
    b.composite = bind(*r.layouts.composite, {tex(0, t.lit), tex(1, t.aux), tex(2, t.light), tex(3, t.bloom[0]), samp(4), ubo(5, r.composite_ubo, sizeof(GpuComposite)), view_entry(6, white)});
    b.lit_src = bind(*r.layouts.blit, {tex(0, t.lit), samp(1)});
    for (uint32_t i = 0; i < BLOOM_MIPS; ++i) {
        b.bloom_src[i] = bind(*r.layouts.blit, {tex(0, t.bloom[i]), samp(1)});
    }
    b.post_a = bind(*r.layouts.post, {tex(0, t.ldr_a), tex(1, t.distortion), samp(2), ubo(3, r.post_ubo, sizeof(GpuPost))});
    b.post_b = bind(*r.layouts.post, {tex(0, t.ldr_b), tex(1, t.distortion), samp(2), ubo(3, r.post_ubo, sizeof(GpuPost))});
    b.blur_a = bind(*r.layouts.blit, {tex(0, t.ldr_a), samp(1)});
    b.blur_b = bind(*r.layouts.blit, {tex(0, t.ldr_b), samp(1)});
    b.transition = bind(*r.layouts.transition, {tex(0, t.composed), tex(1, t.ui), tex(2, t.snapshot), samp(3), ubo(4, r.transition_ubo, sizeof(GpuTransition))});
    b.compose = bind(*r.layouts.compose, {tex(0, t.composed), tex(1, t.ui), samp(2)});
    b.light_one_src = bind(*r.layouts.blit, {tex(0, t.light_one), samp(1)});
    b.light_pong_src = bind(*r.layouts.blit, {tex(0, t.light_pong), samp(1)});

    if (r.particles.particles.buffer) {
        particles_rebind(r);
    }
}

void Render::resize_minimap(RenderState& r, uint32_t pixels) {
    pixels = std::clamp(pixels, 64U, 1024U);
    make_target(r, r.targets.minimap, pixels, pixels, FORMAT_LDR);
    r.minimap_clay.view = *r.targets.minimap.view;
    r.minimap_clay.sampler = *r.samplers.linear;
    r.minimap_clay.uv = {0.0F, 0.0F, 1.0F, 1.0F};
}

auto Render::camera_uniform(const RenderState& r, glm::vec2 center, glm::vec2 extent_px, float zoom, float rotation, glm::vec2 shake) -> GpuCamera {
    GpuCamera out = {};
    out.center = center;
    out.extent = extent_px / std::max(zoom, 0.0001F);
    out.screen = extent_px;
    out.shake = shake;
    out.zoom = zoom;
    out.rotation = rotation;
    out.time = static_cast<float>(r.time);
    out.dpi = r.dpi;
    return out;
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
