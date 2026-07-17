#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace builtin {
inline constexpr uint64_t WHITE = 1;
inline constexpr uint64_t DISC = 2;
inline constexpr uint64_t NOISE = 3;
inline constexpr uint64_t LAST = 15;
}

inline constexpr float SPRITE_TEXELS_PER_UNIT = 2.0F;

struct Color {
    glm::vec3 value{1.0F, 1.0F, 1.0F};
};

struct Decoration {};

struct Hidden {};

struct PrevPose {
    glm::vec2 position{0};
    float angle = 0;
};

struct FrameMix {
    float alpha = 1.0F;
};

enum class BlendMode : uint8_t {
    Normal = 0,
    Add = 1,
    Multiply = 2,
    Modulate = 3,
    Screen = 4,
    Subtract = 5,
};
inline constexpr uint32_t BLEND_MODES = 6;

struct Blend {
    float opacity = 1.0F;
    BlendMode mode = BlendMode::Normal;
};

struct RenderPlane {
    int16_t value = 0;

    constexpr auto operator<=>(const RenderPlane&) const = default;
    [[nodiscard]] constexpr auto operator+(int offset) const -> RenderPlane { return {static_cast<int16_t>(value + offset)}; }
    [[nodiscard]] constexpr auto operator-(int offset) const -> RenderPlane { return {static_cast<int16_t>(value - offset)}; }
};

namespace plane {
inline constexpr RenderPlane Background{-30000};
inline constexpr RenderPlane Floor{-20000};
inline constexpr RenderPlane Decal{-10000};
inline constexpr RenderPlane Entity{0};
inline constexpr RenderPlane Overhead{10000};
inline constexpr RenderPlane Foreground{20000};
}

struct RenderDepth {
    RenderPlane plane = plane::Entity;
    bool y_sort = false;
};

struct Attach {
    uint64_t parent = 0;
    glm::vec2 offset{0.0F};
    float rotation = 0.0F;
    bool inherit_rotation = true;
};

struct Sprite {
    uint64_t texture = 0;
    glm::vec2 size{0.0F};
    glm::vec2 pivot{0.5F, 0.5F};
    glm::vec2 offset{0.0F};
    glm::vec4 region{0.0F, 0.0F, 1.0F, 1.0F};
    bool flip_x = false;
    bool flip_y = false;
};

struct SpritePart {
    uint8_t index = 0;
};

struct Material {
    uint64_t shader = 0;
    uint64_t normal_map = 0;
    float emissive = 0.0F;
    float dissolve = 0.0F;
    glm::vec4 dissolve_edge{1.0F, 0.35F, 0.05F, 1.0F};
    float distortion = 0.0F;
    float params[8] = {};
};

struct Light {
    glm::vec3 color{1.0F, 1.0F, 1.0F};
    float radius = 220.0F;
    float intensity = 1.0F;
    float cone = 0.0F;
    float softness = 0.5F;
    bool shadows = true;
    float flicker = 0.0F;
};

struct Occluder {
    glm::vec2 half{16.0F, 16.0F};
    float opacity = 1.0F;
};

struct RadarVisible {
    glm::vec3 color{1.0F, 1.0F, 1.0F};
    float radius = 0.0F;
    bool through_walls = false;
};

struct VisionBlocker {
    float radius = 120.0F;
    float strength = 1.0F;
    glm::vec3 color{0.59F, 0.59F, 0.61F};
};

enum class VisionKind : uint8_t {
    None,
    Radial,
    Cone,
};

struct Vision {
    VisionKind kind = VisionKind::None;
    float range = 600.0F;
    float angle = 1.2F;
    bool solid = false;
    float ambient = 1.0F;
    glm::vec3 ambient_color{1.0F, 1.0F, 1.0F};
};

struct Camera {
    uint64_t target = 0;
    glm::vec2 focus{0.0F};
    glm::vec2 offset{0.0F};
    float zoom = 1.0F;
    float rotation = 0.0F;
    float follow = 0.16F;
    float shake = 0.0F;
};

struct PostStack {
    glm::vec4 tint{0.0F, 0.0F, 0.0F, 0.0F};
    float flash = 0.0F;
    float flash_fade = 1.0F;
    float vignette = 0.0F;
    float blur = 0.0F;
    float chromatic = 0.0F;
    float pixelate = 0.0F;
    float crt = 0.0F;
    float dither = 0.0F;
    float saturation = 1.0F;
    float distortion = 1.0F;
};

struct Environment {
    glm::vec3 background{0.9F, 0.9F, 0.9F};
    uint64_t texture = 0;
    float texture_size = 0.0F;
    glm::vec3 modulate{1.0F, 1.0F, 1.0F};
};

struct Loading {
    float active = 0.0F;
};

struct ParticleEmitter {
    float rate = 0.0F;
    uint16_t burst = 0;
    uint64_t texture = 0;
    glm::vec2 spawn_half{0.0F};
    float direction = 0.0F;
    float spread = 6.2832F;
    glm::vec2 speed{40.0F, 120.0F};
    glm::vec2 size{8.0F, 18.0F};
    glm::vec2 life{0.5F, 1.0F};
    float gravity = 0.0F;
    float drag = 1.5F;
    float spin = 9.0F;
    float grow = 0.0F;
    glm::vec4 color_begin{1.0F, 1.0F, 1.0F, 1.0F};
    glm::vec4 color_end{1.0F, 1.0F, 1.0F, 0.0F};
    float emissive = 0.0F;
    float bounce = 0.0F;
    bool collide = false;
    bool local_space = false;
    BlendMode blend = BlendMode::Normal;
};

struct RequestParticles {
    glm::vec2 position{0.0F};
    uint16_t count = 12;
    uint64_t texture = 0;
    float direction = 0.0F;
    float spread = 6.2832F;
    glm::vec2 speed{40.0F, 120.0F};
    glm::vec2 size{8.0F, 18.0F};
    glm::vec2 life{0.5F, 1.0F};
    float gravity = 0.0F;
    float drag = 1.5F;
    float spin = 9.0F;
    float grow = 0.0F;
    glm::vec4 color_begin{1.0F, 1.0F, 1.0F, 1.0F};
    glm::vec4 color_end{1.0F, 1.0F, 1.0F, 0.0F};
    float emissive = 0.0F;
    float bounce = 0.0F;
    bool collide = false;
    BlendMode blend = BlendMode::Normal;
};

enum class ScreenTransitionKind : uint8_t {
    Fade = 0,
    Slide = 1,
    Dissolve = 2,
    Pixelate = 3,
    Circle = 4,
};

enum class TransitionScope : uint8_t {
    Frame = 0,
    Interface = 1,
};

enum class SlideMode : uint8_t {
    Push = 0,
    Cover = 1,
    Reveal = 2,
};

struct RequestTransition {
    ScreenTransitionKind kind = ScreenTransitionKind::Fade;
    float duration = 0.25F;
    glm::vec4 color{0.0F, 0.0F, 0.0F, 1.0F};
    glm::vec2 center{0.5F, 0.5F};
    uint8_t direction = 0;
    TransitionScope scope = TransitionScope::Frame;
    SlideMode slide = SlideMode::Push;
};
