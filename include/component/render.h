#pragma once

#include <cstdint>

#include <glm/glm.hpp>

struct Color {
    glm::vec3 value;
};

struct Decoration {};

enum class BlendMode : uint8_t {
    Normal = 0,
    Add = 1,
    Multiply = 2,
    Modulate = 3,
};

struct Blend {
    float opacity = 1.0F;
    BlendMode mode = BlendMode::Normal;
};

enum class RenderLayer : uint8_t {
    Underlay = 0,
    Ground = 1,
    Overlay = 2,
};

struct Layer {
    RenderLayer value = RenderLayer::Ground;
};

struct VisionBlocker {
    float radius = 120.0F;
    float alpha = 1.0F;
    float r = 150;
    float g = 150;
    float b = 155;
};

enum class VisionKind : uint8_t {
    None,
    Radial,
    Cone,
};

struct Camera {
    uint64_t target = 0;
    float focus_x = 0;
    float focus_y = 0;
    float offset_x = 0;
    float offset_y = 0;
    float zoom = 1.0F;
    float rotation = 0.0F;
    float follow = 0.16F;
    float shake = 0.0F;

    float ambient = 1.0F;
    VisionKind vision = VisionKind::None;
    float vision_range = 600.0F;
    float vision_angle = 1.2F;

    float tint_r = 0;
    float tint_g = 0;
    float tint_b = 0;
    float tint_a = 0;
    float flash = 0;
    float flash_fade = 1.0F;
    float vignette = 0;
    float blur = 0;
    float chromatic = 0;
    float shadow_solid = 0;
};

struct Light {
    float r = 255;
    float g = 255;
    float b = 255;
    float radius = 220.0F;
    float intensity = 1.0F;
};
