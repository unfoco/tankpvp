
#include "render.h"

#include <cstring>


void Render::particles_setup(RenderState& r) {
    auto make = [&](GpuBuffer& buf, uint64_t size, WGPUBufferUsage usage) -> void {
        WGPUBufferDescriptor d = {};
        d.usage = usage;
        d.size = size;
        buf.buffer = r.device->createBuffer(d);
        buf.capacity = size;
    };
    r.particles.capacity = r.quality.particle_cap;
    make(r.particles.particles, static_cast<uint64_t>(r.particles.capacity) * sizeof(GpuParticle), WGPUBufferUsage_Storage);
    make(r.particles.emitters, static_cast<uint64_t>(MAX_EMITTERS) * sizeof(GpuEmitter), WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    make(r.particles.counters, sizeof(GpuParticleCounters), WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    make(r.particles.alive, static_cast<uint64_t>(r.particles.capacity) * sizeof(uint32_t), WGPUBufferUsage_Storage);
    make(r.particles.indirect, sizeof(GpuDrawIndirect), WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst);

    GpuParticleCounters counters = {.spawn_total = 0, .dt_bits = 0, .capacity = r.particles.capacity};
    r.queue->writeBuffer(*r.particles.counters.buffer, 0, &counters, sizeof(counters));
}

void Render::particles_rebind(RenderState& r) {
    auto& p = r.particles;
    if (!p.particles.buffer || !r.targets.occluder.view) {
        return;
    }
    {
        WGPUBindGroupEntry entries[7] = {};
        entries[0].binding = 0;
        entries[0].buffer = *p.particles.buffer;
        entries[0].size = p.particles.capacity;
        entries[1].binding = 1;
        entries[1].buffer = *p.indirect.buffer;
        entries[1].size = p.indirect.capacity;
        entries[2].binding = 2;
        entries[2].buffer = *p.emitters.buffer;
        entries[2].size = p.emitters.capacity;
        entries[3].binding = 3;
        entries[3].buffer = *p.counters.buffer;
        entries[3].size = p.counters.capacity;
        entries[4].binding = 4;
        entries[4].textureView = *r.targets.occluder.view;
        entries[5].binding = 5;
        entries[5].buffer = *r.occluder_ubo.buffer;
        entries[5].size = sizeof(GpuCamera);
        entries[6].binding = 6;
        entries[6].buffer = *p.alive.buffer;
        entries[6].size = p.alive.capacity;
        WGPUBindGroupDescriptor d = {};
        d.layout = *r.layouts.particles;
        d.entryCount = 7;
        d.entries = entries;
        p.sim_bind = r.device->createBindGroup(d);
    }
    {
        WGPUBindGroupEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].textureView = *r.particle_atlas.view;
        entries[1].binding = 1;
        entries[1].sampler = *r.samplers.linear;
        entries[2].binding = 2;
        entries[2].buffer = *p.particles.buffer;
        entries[2].size = p.particles.capacity;
        entries[3].binding = 3;
        entries[3].buffer = *p.alive.buffer;
        entries[3].size = p.alive.capacity;
        WGPUBindGroupDescriptor d = {};
        d.layout = *r.layouts.particle_draw;
        d.entryCount = 4;
        d.entries = entries;
        p.draw_bind = r.device->createBindGroup(d);
    }
}

void Render::particles_upload(RenderState& r, float dt) {
    if (r.particles.bound_atlas != *r.particle_atlas.view) {
        r.particles.bound_atlas = *r.particle_atlas.view;
        particles_rebind(r);
    }

    GpuDrawIndirect indirect = {.vertex_count = 6, .instance_count = 0, .first_vertex = 0, .first_instance = 0};
    r.queue->writeBuffer(*r.particles.indirect.buffer, 0, &indirect, sizeof(indirect));

    uint32_t dt_bits = 0;
    std::memcpy(&dt_bits, &dt, sizeof(dt));
    r.queue->writeBuffer(*r.particles.counters.buffer, offsetof(GpuParticleCounters, dt_bits), &dt_bits, sizeof(dt_bits));
    uint32_t capacity = r.particles.capacity;
    r.queue->writeBuffer(*r.particles.counters.buffer, offsetof(GpuParticleCounters, capacity), &capacity, sizeof(capacity));

    auto& padded = r.emitter_upload;
    padded.assign(MAX_EMITTERS, GpuEmitter{});
    const auto& emitters = r.scratch.emitters;
    std::copy(emitters.begin(), emitters.end(), padded.begin());
    r.queue->writeBuffer(*r.particles.emitters.buffer, 0, padded.data(), padded.size() * sizeof(GpuEmitter));
}

void Render::particles_simulate(RenderState& r, WGPUCommandEncoder encoder) {
    wgpu::CommandEncoder enc(encoder);
    wgpu::ComputePassEncoder pass = enc.beginComputePass();
    pass.setBindGroup(0, *r.particles.sim_bind, 0, nullptr);
    if (!r.scratch.emitters.empty()) {
        pass.setPipeline(*r.pipelines.particle_emit);
        pass.dispatchWorkgroups(static_cast<uint32_t>(r.scratch.emitters.size()), 1, 1);
    }
    pass.setPipeline(*r.pipelines.particle_sim);
    pass.dispatchWorkgroups((r.particles.capacity + 63) / 64, 1, 1);
    pass.end();
    pass.release();
}

void Render::particles_draw(RenderState& r, WGPURenderPassEncoder pass, bool distortion) {
    wgpu::RenderPassEncoder p(pass);
    p.setPipeline(distortion ? *r.pipelines.particle_distort : *r.pipelines.particle_draw);
    p.setBindGroup(1, *r.particles.draw_bind, 0, nullptr);
    p.drawIndirect(*r.particles.indirect.buffer, 0);
}

