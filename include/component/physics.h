#pragma once

#include <flecs.h>

#include <glm/glm.hpp>

#include "util/fixed_buffer.h"

struct Static {};
struct Dynamic {};
struct Kinematic {};

struct CollisionBox {
    float height = 1.0F;
    float width = 1.0F;
};
struct CollisionRing {
    float radius = 0.5F;
};

struct VelocityLinear {
    glm::vec2 value{0};
};
struct VelocityAngular {
    float value = 0;
};
struct ExternalForce {
    glm::vec2 value{0};
};
struct ExternalImpulse {
    glm::vec2 value{0};
};

struct Density {
    float value = 1.0F;
};
struct Friction {
    float value = 0.3F;
};
struct Restitution {
    float value = 0.0F;
};
struct DampingLinear {
    float value = 0.0F;
};
struct DampingAngular {
    float value = 0.0F;
};

struct CollisionLayers {
    uint64_t memberships = 0x0001;
    uint64_t filter = 0xFFFFFFFFFFFFFFFF;
};

struct CollisionContinuous {};
struct RotationFixed {};
struct Teleport {};
struct Sensor {};

struct PhysicsConfig {
    glm::vec2 gravity{0, 0};
    int sub_steps = 4;
};

struct ContactEvent {
    flecs::entity entity_a, entity_b;
    glm::vec2 point{0}, normal{0};
};

struct PhysicsEvents {
    FixedBuffer<ContactEvent, 64> contactBegin, sensorBegin;
    FixedBuffer<ContactEvent, 64> contactEnd, sensorEnd;

    void clear() {
        contactBegin.clear();
        sensorBegin.clear();
        contactEnd.clear();
        sensorEnd.clear();
    }

    template <typename A, typename B>
    struct View {
        const ContactEvent* ptr;
        const ContactEvent* last;

        void skip() {
            while (ptr != last) {
                if (ptr->entity_a.is_alive() && ptr->entity_b.is_alive() &&
                    ((ptr->entity_a.template has<A>() && ptr->entity_b.template has<B>()) || (ptr->entity_b.template has<A>() && ptr->entity_a.template has<B>()))) {
                    return;
                }
                ++ptr;
            }
        }

        auto operator*() const -> std::pair<flecs::entity, flecs::entity> {
            if (ptr->entity_a.template has<A>()) {
                return {ptr->entity_a, ptr->entity_b};
            }
            return {ptr->entity_b, ptr->entity_a};
        }
        auto operator++() -> View& {
            ++ptr;
            skip();
            return *this;
        }
        auto operator!=(const View& o) const -> bool {
            return ptr != o.ptr;
        }

        auto begin() const -> View {
            View v{ptr, last};
            v.skip();
            return v;
        }
        auto end() const -> View {
            return {last, last};
        }
    };

    template <typename A, typename B>
    auto sensor() const {
        return View<A, B>{sensorBegin.begin(), sensorBegin.end()};
    }
    template <typename A, typename B>
    auto contact() const {
        return View<A, B>{contactBegin.begin(), contactBegin.end()};
    }
    template <typename A, typename B>
    auto sensor_end() const {
        return View<A, B>{sensorEnd.begin(), sensorEnd.end()};
    }
    template <typename A, typename B>
    auto contact_end() const {
        return View<A, B>{contactEnd.begin(), contactEnd.end()};
    }
};

struct RequestRaycast {
    glm::vec2 origin, direction;
    float range;
};

struct ResponseRaycast {
    struct Hit {
        flecs::entity entity;
        glm::vec2 point{0}, normal{0};
        float fraction = 0;
    };
    FixedBuffer<Hit, 32> hits;
};

struct RequestAreaQuery {
    glm::vec2 center;
    float radius;
};

struct ResponseAreaQuery {
    struct Hit {
        flecs::entity entity;
        float distance;
    };
    FixedBuffer<Hit, 64> hits;
};

struct RequestExplosion {
    glm::vec2 center;
    float radius;
    float force;
    float damage;
    flecs::entity source;
};

struct ResponseExplosion {
    struct Hit {
        flecs::entity entity;
        float intensity;
    };
    FixedBuffer<Hit, 64> hits;
};
