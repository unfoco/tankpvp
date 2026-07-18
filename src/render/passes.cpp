
#include "render.h"

#include <algorithm>
#include <functional>

#include "component/object.h"
#include "component/script.h"
#include "component/settings.h"
#include "component/world.h"
#include "util/ballistics.h"
#include "util/time.h"

namespace {

auto color_attachment(WGPUTextureView view, WGPULoadOp load, WGPUColor clear = {0, 0, 0, 0}) -> WGPURenderPassColorAttachment {
    WGPURenderPassColorAttachment a = {};
    a.view = view;
    a.loadOp = load;
    a.storeOp = WGPUStoreOp_Store;
    a.clearValue = clear;
    a.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    return a;
}

auto begin_pass(WGPUCommandEncoder encoder, std::vector<WGPURenderPassColorAttachment> attachments) -> wgpu::RenderPassEncoder {
    WGPURenderPassDescriptor d = {};
    d.colorAttachmentCount = attachments.size();
    d.colorAttachments = attachments.data();
    return wgpu::CommandEncoder(encoder).beginRenderPass(d);
}

auto state_of(flecs::iter& it) -> RenderState* {
    auto* r = it.world().try_get_mut<RenderState>();
    return (r != nullptr && r->frame.ok) ? r : nullptr;
}

void fullscreen_pass(RenderState& r, WGPUCommandEncoder enc, WGPURenderPipeline pipeline, WGPUTextureView target, WGPUBindGroup group1, WGPULoadOp load = WGPULoadOp_Clear) {
    auto pass = begin_pass(enc, {color_attachment(target, load)});
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
    pass.setBindGroup(1, group1, 0, nullptr);
    pass.draw(6, 1, 0, 0);
    pass.end();
    pass.release();
}

void draw_tiles(RenderState& r, wgpu::RenderPassEncoder& pass, bool solid, WGPURenderPipeline pipeline, WGPUBindGroup frame, bool with_atlas) {
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, frame, 0, nullptr);
    if (with_atlas) {
        pass.setBindGroup(1, *r.tile_atlas.bind, 0, nullptr);
    }
    glm::vec2 half = {static_cast<float>(r.targets.occluder.width) * 0.5F / std::max(r.camera.zoom, 0.0001F), static_cast<float>(r.targets.occluder.height) * 0.5F / std::max(r.camera.zoom, 0.0001F)};
    constexpr float CHUNK_UNITS = 32.0F * 32.0F;
    for (auto& [key, mesh] : r.tiles.chunks) {
        uint32_t count = solid ? mesh.solid_count : mesh.floor_count;
        const GpuBuffer& buf = solid ? mesh.solid : mesh.floor;
        if (count == 0 || !buf.buffer) {
            continue;
        }
        auto cx = static_cast<float>(static_cast<int32_t>(key >> 32));
        auto cy = static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(key)));
        glm::vec2 center = {(cx + 0.5F) * CHUNK_UNITS, (cy + 0.5F) * CHUNK_UNITS};
        if (std::abs(center.x - r.camera.position.x) > half.x + CHUNK_UNITS || std::abs(center.y - r.camera.position.y) > half.y + CHUNK_UNITS) {
            continue;
        }
        pass.setVertexBuffer(0, *buf.buffer, 0, static_cast<uint64_t>(count) * sizeof(GpuTile));
        pass.draw(6, count, 0, 0);
    }
}

}

void Render::draw_runs(flecs::world world, RenderState& r, WGPURenderPassEncoder raw, size_t first_run, size_t last_run, bool distortion) {
    if (first_run >= last_run || !r.sprite_instances.buffer) {
        return;
    }
    wgpu::RenderPassEncoder pass(raw);
    pass.setVertexBuffer(0, *r.sprite_instances.buffer, 0, r.scratch.instances.size() * sizeof(GpuInstance));
    for (size_t i = first_run; i < last_run && i < r.scratch.runs.size(); ++i) {
        const DrawRun& run = r.scratch.runs[i];
        if (distortion) {
            pass.setPipeline(*r.pipelines.sprite_distort);
        } else {
            pass.setPipeline(material_pipeline(world, r, run.shader, run.blend));
        }
        pass.setBindGroup(1, bind_material(world, r, run.texture, run.normal_map, run.nearest), 0, nullptr);
        pass.draw(6, run.count, 0, run.first);
    }
}

void Render::begin(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto* rp = world.try_get_mut<RenderState>();
        if (rp == nullptr) {
            return;
        }
        RenderState& r = *rp;
        r.frame.ok = false;
        r.frame_index++;

        int pw = 0;
        int ph = 0;
        SDL_GetWindowSizeInPixels(r.window, &pw, &ph);
        if (pw <= 0 || ph <= 0) {
            return;
        }
        bool quality_changed = false;
        if (const auto* settings = world.try_get<Settings>()) {
            float internal = std::clamp(settings->render_scale, 0.25F, 1.0F);
            float light = std::clamp(settings->light_scale, 0.125F, 1.0F);
            quality_changed = internal != r.quality.internal_scale || light != r.quality.light_scale;
            r.quality.internal_scale = internal;
            r.quality.light_scale = light;
            r.quality.bloom = settings->bloom;
        }
        if (quality_changed || static_cast<uint32_t>(pw) != r.targets.composed.width || static_cast<uint32_t>(ph) != r.targets.composed.height) {
            r.surface->configure(WGPUSurfaceConfiguration{
                .device = *r.device,
                .format = r.surface_format,
                .usage = WGPUTextureUsage_RenderAttachment,
                .width = static_cast<uint32_t>(pw),
                .height = static_cast<uint32_t>(ph),
                .alphaMode = WGPUCompositeAlphaMode_Auto,
                .presentMode = WGPUPresentMode_Fifo,
            });
            Render::resize_targets(r, static_cast<uint32_t>(pw), static_cast<uint32_t>(ph));
        }
        int lw = 0;
        int lh = 0;
        SDL_GetWindowSize(r.window, &lw, &lh);
        float density = lw > 0 ? static_cast<float>(pw) / static_cast<float>(lw) : 1.0F;
        r.dpi = std::clamp(static_cast<float>(ph) / static_cast<float>(HEIGHT), 1.0F, 4.0F);
        world.set<UiScale>({.dpi = r.dpi, .density = density});

        if (r.frame.surface.texture != nullptr) {
            wgpuTextureRelease(r.frame.surface.texture);
            r.frame.surface = {};
        }
        wgpuSurfaceGetCurrentTexture(*r.surface, &r.frame.surface);
        if (r.frame.surface.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && r.frame.surface.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            if (r.frame.surface.texture != nullptr) {
                wgpuTextureRelease(r.frame.surface.texture);
                r.frame.surface = {};
            }
            return;
        }
        r.frame.backbuffer = wgpu::raii::TextureView(wgpuTextureCreateView(r.frame.surface.texture, nullptr));
        r.frame.encoder = r.device->createCommandEncoder();
        r.frame.ok = true;
        r.textures.transient.clear();

        if (r.transition.capture_pending) {
            r.transition.capture_pending = false;
            if (r.transition.scope == TransitionScope::Interface) {
                WGPUTexelCopyTextureInfo src = {};
                src.texture = *r.targets.ui.texture;
                src.aspect = WGPUTextureAspect_All;
                WGPUTexelCopyTextureInfo dst = src;
                dst.texture = *r.targets.snapshot.texture;
                r.frame.encoder->copyTextureToTexture(src, dst, {r.targets.ui.width, r.targets.ui.height, 1});
            } else {
                auto pass = begin_pass(*r.frame.encoder, {color_attachment(*r.targets.snapshot.view, WGPULoadOp_Clear)});
                pass.setPipeline(*r.pipelines.compose);
                pass.setBindGroup(0, *r.binds.compose, 0, nullptr);
                pass.draw(6, 1, 0, 0);
                pass.end();
                pass.release();
            }
            r.transition.start = util::now();
        }
    }
}

void Render::pass_occluder(flecs::iter& it) {
    while (it.next()) {
        RenderState* rp = state_of(it);
        if (rp == nullptr) {
            return;
        }
        RenderState& r = *rp;
        auto pass = begin_pass(*r.frame.encoder, {color_attachment(*r.targets.occluder.view, WGPULoadOp_Clear)});
        draw_tiles(r, pass, true, *r.pipelines.tile_mask, *r.binds.frame_occluder, false);
        if (!r.scratch.occluders.empty() && r.occluder_instances.buffer) {
            pass.setPipeline(*r.pipelines.occluder);
            pass.setBindGroup(0, *r.binds.frame_occluder, 0, nullptr);
            pass.setVertexBuffer(0, *r.occluder_instances.buffer, 0, r.scratch.occluders.size() * sizeof(GpuOccluder));
            pass.draw(6, static_cast<uint32_t>(r.scratch.occluders.size()), 0, 0);
        }
        pass.end();
        pass.release();
    }
}

void Render::compute(flecs::iter& it) {
    while (it.next()) {
        RenderState* rp = state_of(it);
        if (rp == nullptr) {
            return;
        }
        Render::particles_simulate(*rp, *rp->frame.encoder);
    }
}

void Render::pass_world(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        RenderState* rp = state_of(it);
        if (rp == nullptr) {
            return;
        }
        RenderState& r = *rp;

        {
            auto pass = begin_pass(*r.frame.encoder, {
                color_attachment(*r.targets.scene.view, WGPULoadOp_Clear, {0, 0, 0, 1}),
                color_attachment(*r.targets.aux.view, WGPULoadOp_Clear, {0.5, 0.5, 0, 0}),
            });
            pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
            if (r.sprite_instances.buffer) {
                const SlotInfo& slot = r.scratch.slots[r.scratch.instances[0].slot];
                pass.setPipeline(*r.pipelines.background);
                pass.setBindGroup(1, Render::bind_material(world, r, slot.texture, 0, false), 0, nullptr);
                pass.setVertexBuffer(0, *r.sprite_instances.buffer, 0, r.scratch.instances.size() * sizeof(GpuInstance));
                pass.draw(6, 1, 0, 0);
            }
            Render::draw_runs(world, r, pass, 0, r.params.runs_floor, false);
            draw_tiles(r, pass, false, *r.pipelines.tile, *r.binds.frame, true);
            Render::draw_runs(world, r, pass, r.params.runs_floor, r.params.runs_entities, false);
            pass.end();
            pass.release();
        }

        {
            auto pass = begin_pass(*r.frame.encoder, {
                color_attachment(*r.targets.entities.view, WGPULoadOp_Clear, {0, 0, 0, 0}),
                color_attachment(*r.targets.aux.view, WGPULoadOp_Load),
            });
            pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
            Render::draw_runs(world, r, pass, r.params.runs_entities, r.params.runs_overhead, false);
            Render::particles_draw(r, pass, false);
            pass.end();
            pass.release();
        }

        {
            auto pass = begin_pass(*r.frame.encoder, {
                color_attachment(*r.targets.overhead.view, WGPULoadOp_Clear, {0, 0, 0, 0}),
                color_attachment(*r.targets.aux.view, WGPULoadOp_Load),
            });
            if (!r.params.vision_solid) {
                draw_tiles(r, pass, true, *r.pipelines.tile, *r.binds.frame, true);
            }
            pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
            Render::draw_runs(world, r, pass, r.params.runs_overhead, r.scratch.runs.size(), false);
            pass.end();
            pass.release();
        }
    }
}

void Render::pass_distortion(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        RenderState* rp = state_of(it);
        if (rp == nullptr) {
            return;
        }
        RenderState& r = *rp;
        auto pass = begin_pass(*r.frame.encoder, {color_attachment(*r.targets.distortion.view, WGPULoadOp_Clear)});
        pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
        for (size_t i = 0; i < r.scratch.runs.size(); ++i) {
            const DrawRun& run = r.scratch.runs[i];
            for (uint32_t k = run.first; k < run.first + run.count; ++k) {
                if (r.scratch.instances[k].distortion != 0.0F) {
                    Render::draw_runs(world, r, pass, i, i + 1, true);
                    break;
                }
            }
        }
        Render::particles_draw(r, pass, true);
        pass.end();
        pass.release();
    }
}

void Render::pass_light(flecs::iter& it) {
    while (it.next()) {
        RenderState* rp = state_of(it);
        if (rp == nullptr) {
            return;
        }
        RenderState& r = *rp;
        WGPUCommandEncoder enc = *r.frame.encoder;
        const auto& scratch = r.scratch;
        uint64_t light_bytes = (scratch.lights.size() + scratch.smoke.size()) * sizeof(GpuLight);
        double visibility = r.params.vision ? 0.0 : 1.0;

        {
            auto pass = begin_pass(enc, {color_attachment(*r.targets.light.view, WGPULoadOp_Clear, {0, 0, 0, visibility})});
            if (r.light_instances.buffer && !scratch.lights.empty()) {
                pass.setPipeline(*r.pipelines.light);
                pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
                pass.setBindGroup(1, *r.binds.light, 0, nullptr);
                pass.setVertexBuffer(0, *r.light_instances.buffer, 0, light_bytes);
                pass.draw(6, static_cast<uint32_t>(scratch.lights.size()), 0, 0);
            }
            pass.end();
            pass.release();
        }

        for (size_t i = 0; r.shadow_light_instances.buffer && i < scratch.shadowed.size(); ++i) {
            const ShadowedLight& sl = scratch.shadowed[i];
            {
                auto pass = begin_pass(enc, {color_attachment(*r.targets.light_one.view, WGPULoadOp_Clear)});
                pass.setPipeline(*r.pipelines.light_solid);
                pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
                pass.setBindGroup(1, *r.binds.light, 0, nullptr);
                pass.setVertexBuffer(0, *r.shadow_light_instances.buffer, 0, scratch.shadowed.size() * sizeof(GpuLight));
                pass.draw(6, 1, 0, static_cast<uint32_t>(i));
                if (sl.vert_count > 0 && r.shadow_verts.buffer) {
                    pass.setPipeline(*r.pipelines.shadow_geom);
                    pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
                    pass.setVertexBuffer(0, *r.shadow_verts.buffer, 0, scratch.shadow_verts.size() * sizeof(glm::vec2));
                    pass.draw(sl.vert_count, 1, sl.vert_first, 0);
                }
                pass.end();
                pass.release();
            }
            if (sl.light.softness > 0.05F) {
                for (int b = 0; b < 2; ++b) {
                    fullscreen_pass(r, enc, *r.pipelines.blur_light, *r.targets.light_pong.view, *r.binds.light_one_src);
                    fullscreen_pass(r, enc, *r.pipelines.blur_light, *r.targets.light_one.view, *r.binds.light_pong_src);
                }
            }
            {
                auto pass = begin_pass(enc, {color_attachment(*r.targets.light.view, WGPULoadOp_Load)});
                pass.setPipeline(*r.pipelines.light_accum);
                pass.setBindGroup(0, *r.binds.light_one_src, 0, nullptr);
                pass.draw(6, 1, 0, 0);
                pass.end();
                pass.release();
            }
        }

        if (!scratch.smoke.empty() && r.light_instances.buffer) {
            auto pass = begin_pass(enc, {color_attachment(*r.targets.light.view, WGPULoadOp_Load)});
            pass.setPipeline(*r.pipelines.smoke);
            pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
            pass.setBindGroup(1, *r.binds.light, 0, nullptr);
            pass.setVertexBuffer(0, *r.light_instances.buffer, 0, light_bytes);
            pass.draw(6, static_cast<uint32_t>(scratch.smoke.size()), 0, static_cast<uint32_t>(scratch.lights.size()));
            pass.end();
            pass.release();
        }
    }
}

void Render::pass_composite(flecs::iter& it) {
    while (it.next()) {
        RenderState* rp = state_of(it);
        if (rp == nullptr) {
            return;
        }
        RenderState& r = *rp;
        WGPUCommandEncoder enc = *r.frame.encoder;

        fullscreen_pass(r, enc, *r.pipelines.lit, *r.targets.lit.view, *r.binds.lit);

        if (r.quality.bloom) {
            fullscreen_pass(r, enc, *r.pipelines.bloom_threshold, *r.targets.bloom[0].view, *r.binds.lit_src);
            for (uint32_t i = 1; i < BLOOM_MIPS; ++i) {
                fullscreen_pass(r, enc, *r.pipelines.bloom_down, *r.targets.bloom[i].view, *r.binds.bloom_src[i - 1]);
            }
            for (uint32_t i = BLOOM_MIPS - 1; i > 0; --i) {
                fullscreen_pass(r, enc, *r.pipelines.bloom_up, *r.targets.bloom[i - 1].view, *r.binds.bloom_src[i], WGPULoadOp_Load);
            }
        }

        fullscreen_pass(r, enc, *r.pipelines.composite, *r.targets.ldr_a.view, *r.binds.composite);
    }
}

void Render::pass_post(flecs::iter& it) {
    while (it.next()) {
        RenderState* rp = state_of(it);
        if (rp == nullptr) {
            return;
        }
        RenderState& r = *rp;
        WGPUCommandEncoder enc = *r.frame.encoder;

        bool src_is_a = true;
        int passes = std::clamp(static_cast<int>(r.params.blur), 0, 5);
        for (int i = 0; i < passes * 2; ++i) {
            auto pass = begin_pass(enc, {color_attachment(src_is_a ? *r.targets.ldr_b.view : *r.targets.ldr_a.view, WGPULoadOp_Clear)});
            pass.setPipeline(*r.pipelines.blur);
            pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
            pass.setBindGroup(1, src_is_a ? *r.binds.blur_a : *r.binds.blur_b, 0, nullptr);
            pass.draw(6, 1, 0, 0);
            pass.end();
            pass.release();
            src_is_a = !src_is_a;
        }

        auto pass = begin_pass(enc, {color_attachment(*r.targets.composed.view, WGPULoadOp_Clear)});
        pass.setPipeline(*r.pipelines.post);
        pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
        pass.setBindGroup(1, src_is_a ? *r.binds.post_a : *r.binds.post_b, 0, nullptr);
        pass.draw(6, 1, 0, 0);
        pass.end();
        pass.release();
    }
}

void Render::pass_minimap(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        RenderState* rp = state_of(it);
        if (rp == nullptr || !rp->params.minimap_active || !rp->targets.minimap.view) {
            return;
        }
        RenderState& r = *rp;

        FrameScratch& scratch = r.scratch;
        scratch.minimap.clear();

        if (const auto* views = world.try_get<ViewState>()) {
            for (const ViewActive& view : views->views) {
                std::function<void(const ViewWidget&)> walk = [&](const ViewWidget& w) -> void {
                    if (w.kind == ViewKind::Minimap) {
                        for (const Blip& b : w.blips) {
                            GpuInstance dot = {};
                            dot.position = {b.x, b.y};
                            float radius = b.radius > 0.0F ? b.radius : (r.params.minimap_range / r.targets.minimap.width) * 8.0F;
                            dot.size = {radius * 2.0F, radius * 2.0F};
                            dot.pivot = {0.5F, 0.5F};
                            dot.uv = {0, 0, 1, 1};
                            bool area = b.radius > 0.0F;
                            float alpha = area ? std::min(1.0F, (static_cast<float>(b.a) / 255.0F) * 1.8F + 0.12F) : 1.0F;
                            float lift = area ? 1.15F : 1.35F;
                            dot.tint = {std::min(1.0F, static_cast<float>(b.r) / 255.0F * lift), std::min(1.0F, static_cast<float>(b.g) / 255.0F * lift),
                                        std::min(1.0F, static_cast<float>(b.b) / 255.0F * lift), alpha};
                            dot.slot = 0;
                            scratch.minimap.push_back(dot);
                        }
                    }
                    for (const ViewWidget& c : w.children) {
                        walk(c);
                    }
                };
                walk(view.root);
            }
        }

        {
            const auto* grid = world.try_get<WorldGrid>();
            const auto* tileset = world.try_get<Tileset>();
            glm::vec2 center = r.params.minimap_center;
            constexpr float NEAR = 48.0F;
            auto los_blocked = [&](glm::vec2 target) -> bool {
                if (grid == nullptr || tileset == nullptr) {
                    return false;
                }
                glm::vec2 d = target - center;
                int steps = static_cast<int>(glm::length(d) / 20.0F);
                for (int i = 1; i < steps; ++i) {
                    glm::vec2 pp = center + (d * (static_cast<float>(i) / static_cast<float>(steps)));
                    if (ballistics::solid(ballistics::tile_at(*grid, *tileset, pp.x, pp.y))) {
                        return true;
                    }
                }
                return false;
            };
            r.queries.radar.each([&](const Position& p, const RadarVisible& rv) -> void {
                float dist = glm::length(p.value - center);
                if (!rv.through_walls && dist > NEAR && los_blocked(p.value)) {
                    return;
                }
                GpuInstance dot = {};
                dot.position = p.value;
                float radius = rv.radius > 0.0F ? rv.radius : (r.params.minimap_range / static_cast<float>(r.targets.minimap.width)) * 8.0F;
                dot.size = {radius * 2.0F, radius * 2.0F};
                dot.pivot = {0.5F, 0.5F};
                dot.uv = {0, 0, 1, 1};
                dot.tint = {rv.color.r, rv.color.g, rv.color.b, 1.0F};
                scratch.minimap.push_back(dot);
            });
        }

        {
            GpuInstance dot = {};
            dot.position = r.params.minimap_center;
            float radius = (r.params.minimap_range / static_cast<float>(r.targets.minimap.width)) * 10.0F;
            dot.size = {radius * 2.0F, radius * 2.0F};
            dot.pivot = {0.5F, 0.5F};
            dot.uv = {0, 0, 1, 1};
            dot.tint = {1.0F, 1.0F, 1.0F, 1.0F};
            scratch.minimap.push_back(dot);
        }
        Render::write_buffer(r, r.minimap_instances, scratch.minimap.data(), scratch.minimap.size() * sizeof(GpuInstance), WGPUBufferUsage_Vertex);

        auto pass = begin_pass(*r.frame.encoder, {color_attachment(*r.targets.minimap.view, WGPULoadOp_Clear, {0.04, 0.05, 0.07, 0.62})});
        draw_tiles(r, pass, true, *r.pipelines.tile_silhouette, *r.binds.frame_minimap, true);
        if (!scratch.minimap.empty() && r.minimap_instances.buffer) {
            pass.setPipeline(*r.pipelines.sprite_flat);
            pass.setBindGroup(0, *r.binds.frame_minimap, 0, nullptr);
            pass.setBindGroup(1, Render::bind_material(world, r, builtin::DISC, 0, false), 0, nullptr);
            pass.setVertexBuffer(0, *r.minimap_instances.buffer, 0, scratch.minimap.size() * sizeof(GpuInstance));
            pass.draw(6, static_cast<uint32_t>(scratch.minimap.size()), 0, 0);
        }
        pass.end();
        pass.release();
    }
}

void Render::pass_transition(flecs::iter& it) {
    while (it.next()) {
        RenderState* rp = state_of(it);
        if (rp == nullptr) {
            return;
        }
        RenderState& r = *rp;

        float t = 2.0F;
        if (r.transition.start >= 0.0) {
            t = r.transition.duration > 0.0F ? static_cast<float>((util::now() - r.transition.start) / r.transition.duration) : 2.0F;
            if (t >= 1.0F) {
                r.transition.start = -1.0;
            }
        }

        bool active = t < 1.0F && r.transition.start >= 0.0;
        GpuTransition gt = {};
        gt.color = r.transition.color;
        gt.center = r.transition.center;
        gt.t = active ? std::clamp(t, 0.0F, 1.0F) : 2.0F;
        gt.kind = static_cast<uint32_t>(r.transition.kind);
        gt.direction = static_cast<float>(r.transition.direction);
        gt.aspect = r.targets.composed.height > 0 ? static_cast<float>(r.targets.composed.width) / static_cast<float>(r.targets.composed.height) : 1.0F;
        gt.slide = static_cast<uint32_t>(r.transition.slide);
        gt.scope = static_cast<uint32_t>(r.transition.scope);
        r.queue->writeBuffer(*r.transition_ubo.buffer, 0, &gt, sizeof(gt));

        auto pass = begin_pass(*r.frame.encoder, {color_attachment(*r.frame.backbuffer, WGPULoadOp_Clear)});
        pass.setPipeline(*r.pipelines.transition);
        pass.setBindGroup(0, *r.binds.frame, 0, nullptr);
        pass.setBindGroup(1, *r.binds.transition, 0, nullptr);
        pass.draw(6, 1, 0, 0);
        pass.end();
        pass.release();
    }
}

void Render::present(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto* rp = world.try_get_mut<RenderState>();
        if (rp == nullptr || !rp->frame.ok) {
            return;
        }
        RenderState& r = *rp;
        auto cmd = r.frame.encoder->finish();
        r.queue->submit(1, &cmd);
        wgpuCommandBufferRelease(cmd);
        r.frame.encoder = {};
        r.surface->present();
        if (r.frame.surface.texture != nullptr) {
            wgpuTextureRelease(r.frame.surface.texture);
            r.frame.surface = {};
        }
        r.frame.backbuffer = {};
        r.frame.ok = false;
    }
}

void Render::transition(flecs::entity e, const RequestTransition& req) {
    flecs::world world = e.world();
    if (auto* r = world.try_get_mut<RenderState>()) {
        r->transition.kind = req.kind;
        r->transition.duration = std::max(req.duration, 0.01F);
        r->transition.color = req.color;
        r->transition.center = req.center;
        r->transition.direction = req.direction;
        r->transition.scope = req.scope;
        r->transition.slide = req.slide;
        r->transition.capture_pending = true;
        r->transition.start = -1.0;
    }
    e.destruct();
}
