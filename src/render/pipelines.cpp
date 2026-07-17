#include "render.h"
#include "internal.h"
#include "shader.h"
#include "component/asset.h"
#include <array>
#include <string>
#include <vector>

namespace {

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
