#pragma once

#include <glm/glm.hpp>
#include <flecs.h>

#include <type/fixed_buffer.h>

struct Static {};
struct Dynamic {};
struct Kinematic {};

struct ColliderBox { float half_width = 0.5f; float half_height = 0.5f; };
struct ColliderRing { float radius = 0.5f; };

struct LinearVelocity { glm::vec2 value{0}; };
struct AngularVelocity { float value = 0; };
struct ExternalForce { glm::vec2 value{0}; };
struct ExternalImpulse { glm::vec2 value{0}; };

struct Density { float value = 1.0f; };
struct Friction { float value = 0.3f; };
struct Restitution { float value = 0.0f; };
struct LinearDamping { float value = 0.0f; };
struct AngularDamping { float value = 0.0f; };

struct CollisionLayers {
    uint64_t memberships = 0x0001;
    uint64_t filter = 0xFFFFFFFFFFFFFFFF;
};

struct ContinuousCollision {};
struct FixedRotation {};
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
    FixedBuffer<ContactEvent, 64> contact_begin, contact_end;
    FixedBuffer<ContactEvent, 64> sensor_begin, sensor_end;
    void clear() {
        contact_begin.clear(); contact_end.clear();
        sensor_begin.clear();  sensor_end.clear();
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
