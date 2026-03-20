#pragma once

#include <glm/glm.hpp>
#include <flecs.h>

#include <type/fixed_buffer.h>

struct Static {};
struct Dynamic {};
struct Kinematic {};

struct CollisionBox { float width = 1.0f; float height = 1.0f; };
struct CollisionRing { float radius = 0.5f; };

struct VelocityLinear { glm::vec2 value{0}; };
struct VelocityAngular { float value = 0; };
struct ExternalForce { glm::vec2 value{0}; };
struct ExternalImpulse { glm::vec2 value{0}; };

struct Density { float value = 1.0f; };
struct Friction { float value = 0.3f; };
struct Restitution { float value = 0.0f; };
struct DampingLinear { float value = 0.0f; };
struct DampingAngular { float value = 0.0f; };

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
    flecs::entity entityA, entityB;
    glm::vec2 point{0}, normal{0};
};

struct PhysicsEvents {
    FixedBuffer<ContactEvent, 64> contactBegin, sensorBegin;
    FixedBuffer<ContactEvent, 64> contactEnd, sensorEnd;

    void clear() {
        contactBegin.clear(); sensorBegin.clear();
        contactEnd.clear(); sensorEnd.clear();
    }

    template<typename A, typename B>
    struct View {
        const ContactEvent* ptr;
        const ContactEvent* last;

        void skip() {
            while (ptr != last) {
                if (
                    ptr->entityA.is_alive() && ptr->entityB.is_alive() &&
                    (
                        (ptr->entityA.template has<A>() && ptr->entityB.template has<B>()) ||
                        (ptr->entityB.template has<A>() && ptr->entityA.template has<B>())
                    )
                ) return;
                ++ptr;
            }
        }

        std::pair<flecs::entity, flecs::entity> operator*() const {
            if (ptr->entityA.template has<A>()) return {ptr->entityA, ptr->entityB};
            return {ptr->entityB, ptr->entityA};
        }
        View& operator++() { ++ptr; skip(); return *this; }
        bool operator!=(const View& o) const { return ptr != o.ptr; }

        View begin() const { View v{ptr, last}; v.skip(); return v; }
        View end() const { return {last, last}; }
    };

    template<typename A, typename B>
    auto sensor() const { return View<A, B>{sensorBegin.begin(), sensorBegin.end()}; }
    template<typename A, typename B>
    auto contact() const { return View<A, B>{contactBegin.begin(), contactBegin.end()}; }
    template<typename A, typename B>
    auto sensor_end() const { return View<A, B>{sensorEnd.begin(), sensorEnd.end()}; }
    template<typename A, typename B>
    auto contact_end() const { return View<A, B>{contactEnd.begin(), contactEnd.end()}; }
};

struct RaycastRequest {
    glm::vec2 origin, direction;
    float max_dist;
};

struct RaycastResult {
    struct Hit {
        flecs::entity entity;
        glm::vec2 point{0}, normal{0};
        float fraction = 0;
    };
    FixedBuffer<Hit, 32> hits;
};

struct AreaQueryRequest {
    glm::vec2 center;
    float radius;
};

struct AreaQueryResult {
    struct Hit {
        flecs::entity entity;
        float distance;
    };
    FixedBuffer<Hit, 64> hits;
};

struct ExplosionRequest {
    glm::vec2 center;
    float radius;
    float force;
    float damage;
    flecs::entity source;
};

struct ExplosionResult {
    struct Hit {
        flecs::entity entity;
        float intensity;
    };
    FixedBuffer<Hit, 64> hits;
};
