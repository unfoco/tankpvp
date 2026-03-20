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
    FixedBuffer<ContactEvent, 64> contactBegin, contactEnd;
    FixedBuffer<ContactEvent, 64> sensorBegin, sensorEnd;

    void clear() {
        contactBegin.clear(); contactEnd.clear();
        sensorBegin.clear();  sensorEnd.clear();
    }

    template<typename A, typename B, typename F>
    inline void eachSensor(F&& fn) const {
        for (auto& c : sensorBegin) {
            if (!c.entityA.is_alive() || !c.entityB.is_alive()) continue;
            if (c.entityA.has<A>() && c.entityB.has<B>()) fn(c.entityA, c.entityB);
            else if (c.entityB.has<A>() && c.entityA.has<B>()) fn(c.entityB, c.entityA);
        }
    }

    template<typename A, typename B, typename F>
    inline void eachContact(F&& fn) const {
        for (auto& c : contactBegin) {
            if (!c.entityA.is_alive() || !c.entityB.is_alive()) continue;
            if (c.entityA.has<A>() && c.entityB.has<B>()) fn(c.entityA, c.entityB);
            else if (c.entityB.has<A>() && c.entityA.has<B>()) fn(c.entityB, c.entityA);
        }
    }
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
