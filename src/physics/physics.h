#pragma once

#include <flecs.h>

#include "component/object.h"
#include "component/physics.h"

struct B2Body;

struct Physics {
    Physics(flecs::world& world);

   private:
    static void init(flecs::iter& it, size_t i, const Position& pos, const Rotation& rot);
    static void teleport(flecs::entity e, const B2Body& body, const Position& pos, const Rotation& rot);
    static void sync(const B2Body& b, const VelocityLinear& lv, const VelocityAngular& av);
    static void force(const B2Body& body, const ExternalForce& f);
    static void impulse(const B2Body& body, ExternalImpulse& imp);
    static void step(flecs::iter& it);
    static void transform(const B2Body& body, Position& pos, Rotation& rot);
    static void velocity(const B2Body& b, VelocityLinear& lv, VelocityAngular& av);
    static void event(flecs::iter& it);
    static void raycast(flecs::entity e, const RequestRaycast& req);
    static void area(flecs::entity e, const RequestAreaQuery& req);
    static void explosion(flecs::entity e, const RequestExplosion& req);
    static void cleanup(B2Body& body);
    static void ldamp(const B2Body& body, const DampingLinear& d);
    static void adamp(const B2Body& body, const DampingAngular& d);
};
