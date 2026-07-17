#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

struct GpuCamera {
    glm::vec2 center;
    glm::vec2 extent;
    glm::vec2 screen;
    glm::vec2 shake;
    float zoom;
    float rotation;
    float time;
    float dpi;
};
static_assert(sizeof(GpuCamera) == 48);

namespace instance_flags {
constexpr uint32_t FLIP_X = 1U << 0;
constexpr uint32_t FLIP_Y = 1U << 1;
constexpr uint32_t NEAREST = 1U << 2;
constexpr uint32_t NORMAL_MAP = 1U << 3;
constexpr uint32_t MASKABLE = 1U << 4;
}

struct GpuInstance {
    glm::vec2 position;
    glm::vec2 size;
    glm::vec2 pivot;
    glm::vec2 offset;
    glm::vec4 uv;
    glm::vec4 tint;
    float rotation;
    float emissive;
    float dissolve;
    float distortion;
    uint32_t flags;
    uint32_t slot;
    uint32_t _pad0;
    uint32_t _pad1;
    glm::vec4 param0;
    glm::vec4 param1;
};
static_assert(sizeof(GpuInstance) == 128);
static_assert(offsetof(GpuInstance, uv) == 32 && offsetof(GpuInstance, tint) == 48 && offsetof(GpuInstance, param0) == 96);

struct GpuTile {
    glm::vec2 position;
    glm::vec4 uv;
    uint32_t flags;
    float _pad0;
};
static_assert(sizeof(GpuTile) == 32);

namespace light_flags {
constexpr uint32_t SHADOWS = 1U << 0;
constexpr uint32_t CONE = 1U << 1;
constexpr uint32_t VISION = 1U << 2;
constexpr uint32_t SMOKE = 1U << 3;
}

struct GpuLight {
    glm::vec2 position;
    float radius;
    float softness;
    glm::vec4 color;
    float cone;
    float direction;
    float flags;
    float falloff;
};
static_assert(sizeof(GpuLight) == 48);

struct GpuOccluder {
    glm::vec2 position;
    glm::vec2 half;
    float rotation;
    float opacity;
    glm::vec2 _pad0;
};
static_assert(sizeof(GpuOccluder) == 32);

namespace particle_flags {
constexpr uint32_t COLLIDE = 1U << 0;
constexpr uint32_t LOCAL_SPACE = 1U << 1;
constexpr uint32_t SOFT = 1U << 2;
constexpr uint32_t BLEND_SHIFT = 8;
constexpr uint32_t EMITTER_SHIFT = 16;
}

struct GpuParticle {
    glm::vec2 position;
    glm::vec2 velocity;
    glm::vec4 color_begin;
    glm::vec4 color_end;
    float life;
    float max_life;
    float size;
    float grow;
    float rotation;
    float spin;
    float drag;
    float gravity;
    float bounce;
    float emissive;
    uint32_t flags;
    uint32_t texture_slot;
};
static_assert(sizeof(GpuParticle) == 96);
static_assert(offsetof(GpuParticle, color_begin) == 16);

struct GpuEmitter {
    glm::vec2 origin;
    glm::vec2 spawn_half;
    glm::vec2 speed;
    glm::vec2 size;
    glm::vec2 life;
    float direction;
    float spread;
    glm::vec4 color_begin;
    glm::vec4 color_end;
    float gravity;
    float drag;
    float spin;
    float grow;
    float bounce;
    float emissive;
    uint32_t flags;
    uint32_t texture_slot;
    uint32_t spawn_count;
    uint32_t seed;
    uint32_t emitter_index;
    uint32_t _pad0;
};
static_assert(sizeof(GpuEmitter) == 128);

struct GpuParticleCounters {
    uint32_t spawn_total;
    uint32_t dt_bits;
    uint32_t capacity;
};
static_assert(sizeof(GpuParticleCounters) == 12);

struct GpuDrawIndirect {
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
};
static_assert(sizeof(GpuDrawIndirect) == 16);

struct GpuComposite {
    glm::vec4 ambient;
    float bloom;
    float pad0;
    float exposure;
    float visibility;
};
static_assert(sizeof(GpuComposite) == 32);

struct GpuPost {
    glm::vec4 tint;
    float flash;
    float vignette;
    float pad0;
    float chromatic;
    float pixelate;
    float crt;
    float dither;
    float saturation;
    glm::vec2 screen;
    float time;
    float distortion;
};
static_assert(sizeof(GpuPost) == 64);

struct GpuTransition {
    glm::vec4 color;
    glm::vec2 center;
    float t;
    uint32_t kind;
    float direction;
    float aspect;
    uint32_t slide;
    uint32_t scope;
};
static_assert(sizeof(GpuTransition) == 48);
