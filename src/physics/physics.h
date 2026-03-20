#pragma once

#include <flecs.h>

#include "component/physics.h"
#include "component/object.h"

struct B2Body;

struct Physics {
    Physics(flecs::world&);

private:
    static void init(flecs::iter&, size_t, const Position&, const Rotation&);
    static void teleport(flecs::entity, const B2Body&, const Position&, const Rotation&);
    static void sync(const B2Body&, const VelocityLinear&, const VelocityAngular&);
    static void force(const B2Body&, const ExternalForce&);
    static void impulse(const B2Body&, ExternalImpulse&);
    static void step(flecs::iter&);
    static void transform(const B2Body&, Position&, Rotation&);
    static void velocity(const B2Body&, VelocityLinear&, VelocityAngular&);
    static void event(flecs::iter&);
    static void raycast(flecs::entity, const RaycastRequest&);
    static void area(flecs::entity, const AreaQueryRequest&);
    static void explosion(flecs::entity, const ExplosionRequest&);
    static void cleanup(B2Body&);
    static void ldamp(const B2Body&, const DampingLinear&);
    static void adamp(const B2Body&, const DampingAngular&);
};
