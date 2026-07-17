#pragma once

#include "render.h"

#include <string>
#include <vector>

inline auto str(const char* s) -> WGPUStringView {
    return {s, WGPU_STRLEN};
}

constexpr uint32_t ATLAS_START_LAYERS = 64;

struct PipelineSpec {
    WGPUShaderModule module = nullptr;
    const char* vs = "vs_main";
    const char* fs = "fs_main";
    std::vector<WGPUVertexBufferLayout> buffers;
    std::vector<WGPUColorTargetState> targets;
    std::vector<WGPUBindGroupLayout> groups;
    WGPUShaderModule fs_module = nullptr;
};

auto blend_for(BlendMode mode) -> WGPUBlendState;
auto additive_blend() -> WGPUBlendState;
auto max_blend() -> WGPUBlendState;
auto straight_blend() -> WGPUBlendState;
auto smoke_blend() -> WGPUBlendState;
auto make_module(RenderState& r, const std::string& source, const char* label) -> wgpu::raii::ShaderModule;
auto make_pipeline(RenderState& r, const PipelineSpec& spec, const char* label) -> wgpu::raii::RenderPipeline;
auto sprite_layout() -> WGPUVertexBufferLayout;
auto tile_layout() -> WGPUVertexBufferLayout;
auto light_layout() -> WGPUVertexBufferLayout;
auto occluder_layout() -> WGPUVertexBufferLayout;

auto make_builtin(RenderState& r, uint64_t hash) -> GpuTexture;
void atlas_create(RenderState& r, TextureAtlas& atlas, uint32_t texels, uint32_t layers);
