#pragma once

#include <box2d/box2d.h>

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
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

    Prediction() = default;
    ~Prediction() {
        reset();
    }

    Prediction(const Prediction&) = delete;
    auto operator=(const Prediction&) -> Prediction& = delete;
    Prediction(Prediction&& o) noexcept {
        *this = std::move(o);
    }
    auto operator=(Prediction&& o) noexcept -> Prediction& {
        if (this != &o) {
            reset();
            m_ready = o.m_ready;
            m_has_self = o.m_has_self;
            m_world = o.m_world;
            m_self = o.m_self;
            m_others = std::move(o.m_others);
            m_shoved = std::move(o.m_shoved);
            o.m_ready = false;
            o.m_has_self = false;
        }
        return *this;
    }

    [[nodiscard]] auto has_self() const -> bool {
        return m_has_self;
    }

    void sync(std::span<const Tank> tanks);

    auto run(glm::vec2 self_pos, float self_angle, int steps, float dt, const std::function<Velocity(int step, float heading)>& velocity, bool record_contacts) -> Pose;

    [[nodiscard]] auto shoved() const -> const std::unordered_map<uint64_t, Pose>& {
        return m_shoved;
    }

    void reset();

   private:
    struct Body {
        b2BodyId id;
        glm::vec2 pos;
        float angle;
    };

    void ensure();
    auto make_body(const CollisionBox& box, glm::vec2 pos, float angle, float ldamp, float adamp) -> b2BodyId;

    bool m_ready = false;
    bool m_has_self = false;
    b2WorldId m_world{};
    b2BodyId m_self{};
    std::unordered_map<uint64_t, Body> m_others;
    std::unordered_map<uint64_t, Pose> m_shoved;
};
