
#define CLAY_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#define CLAY_RENDERER_WEBGPU_IMPL

#include "render.h"

#include <cstdlib>

void Render::clay_setup(flecs::world, RenderState& r) {
    Clay_SetMaxElementCount(16384);
    Clay_SetMaxMeasureTextCacheWordCount(32768);
    uint32_t min_memory = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(min_memory, malloc(min_memory));
    Clay_Initialize(arena, {}, {.errorHandlerFunction = [](Clay_ErrorData err) -> void {
                        SDL_Log("clay: %.*s", err.errorText.length, err.errorText.chars);
                        exit(EXIT_FAILURE);
                    }});

    r.clay = Clay_WebGPU_Setup(*r.device, *r.queue, static_cast<WGPUTextureFormat>(FORMAT_LDR));
    if (r.clay == nullptr) {
        SDL_Log("render: failed to set up the clay renderer");
        exit(EXIT_FAILURE);
    }
    Clay_WebGPU_Font normal = Clay_WebGPU_AddFont(r.clay, "asset/font/normal.ttf");
    if (normal < 0) {
        SDL_Log("render: failed to load asset/font/normal.ttf");
        exit(EXIT_FAILURE);
    }
    Clay_WebGPU_Font italic = Clay_WebGPU_AddFont(r.clay, "asset/font/italic.ttf");
    r.clay_fonts[0] = normal;
    r.clay_fonts[1] = italic >= 0 ? italic : normal;
}

void Render::interface(flecs::iter& it, size_t, RenderState& render, InterfaceCommands& commands) {
    if (!render.frame.ok || render.clay == nullptr) {
        return;
    }
    auto pw = static_cast<float>(render.targets.composed.width);
    auto ph = static_cast<float>(render.targets.composed.height);
    Clay_WebGPU_SetLayoutDimensions(render.clay, {pw, ph}, render.dpi);

    const bool has_ui = commands.list.length > 0;
    if (has_ui) {
        Clay_WebGPU_Prepare(render.clay, commands.list, render.clay_fonts);
    }

    WGPURenderPassColorAttachment attachment = {};
    attachment.view = *render.targets.ui.view;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.clearValue = {0.0, 0.0, 0.0, 0.0};
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor desc = {};
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &attachment;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(*render.frame.encoder, &desc);
    if (has_ui) {
        Clay_WebGPU_Render(render.clay, pass);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}
