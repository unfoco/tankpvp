#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

#include "component/input.h"
#include "component/object.h"
#include "util/math.h"

namespace controller {

inline void differential(const InputState& in, float angle, const DifferentialStats& stats, glm::vec2& linear, float& angular) {
    glm::vec2 forward = math::heading(angle);
    float drive = std::clamp(in.move.y, -1.0F, 1.0F);
    float steer = std::clamp(in.move.x, -1.0F, 1.0F);
    linear = forward * (drive * stats.speed);
    angular = steer * stats.turn;
}

inline void top_down(const InputState& in, float angle, const TopDownStats& stats, const glm::vec2& current_linear, float dt, glm::vec2& linear, float& angular) {
    glm::vec2 wish = {in.move.x, -in.move.y};
    float len = glm::length(wish);
    if (len > 1.0F) {
        wish /= len;
    }
    glm::vec2 target = wish * stats.speed;
    if (stats.accel > 0.0F && dt > 0.0F) {
        float t = std::min(1.0F, stats.accel * dt);
        linear = current_linear + (target - current_linear) * t;
    } else {
        linear = target;
    }

    angular = 0.0F;
    glm::vec2 aim = in.aim;
    if (glm::dot(aim, aim) > 1e-6F) {
        float want = std::atan2(aim.y, aim.x);
        float diff = math::angle_difference(want, angle);
        angular = (stats.face_rate > 0.0F) ? diff * stats.face_rate : (dt > 0.0F ? diff / dt : 0.0F);
    }
}

inline void platformer(const InputState& in, const glm::vec2& current_linear, bool grounded, const PlatformerStats& stats, glm::vec2& linear, float& angular, float dt) {
    float target = std::clamp(in.move.x, -1.0F, 1.0F) * stats.speed;
    float control = grounded ? stats.accel : stats.accel * stats.air_control;
    float t = (control > 0.0F && dt > 0.0F) ? std::min(1.0F, control * dt) : 1.0F;
    linear.x = current_linear.x + ((target - current_linear.x) * t);
    linear.y = current_linear.y;
    bool jump = in.move.y > 0.5F;
    if (jump && grounded && current_linear.y >= 0.0F) {
        linear.y = -stats.jump;
    }
    angular = 0.0F;
}

}
