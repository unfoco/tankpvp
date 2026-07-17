#include "render.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace {

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
