#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>

#include "component/physics.h"

struct Prediction {
    struct Pose {
        glm::vec2 pos{};
        float angle = 0;
    };

    struct Tank {
        uint64_t id;
        glm::vec2 pos;
        float angle;
        CollisionBox box;
        float ldamp;
        float adamp;
        bool self;
    };

    struct Velocity {
        glm::vec2 linear{};
        float angular = 0;
    };

    Prediction();
    ~Prediction();
    Prediction(const Prediction&) = delete;
    auto operator=(const Prediction&) -> Prediction& = delete;
    Prediction(Prediction&&) noexcept;
    auto operator=(Prediction&&) noexcept -> Prediction&;

    [[nodiscard]] auto has_self() const -> bool;
    void sync(std::span<const Tank> tanks);
    auto run(glm::vec2 self_pos, float self_angle, int steps, float dt, const std::function<Velocity(int step, float heading)>& velocity, bool record_contacts) -> Pose;
    [[nodiscard]] auto shoved() const -> const std::unordered_map<uint64_t, Pose>&;
    void reset();

   private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
