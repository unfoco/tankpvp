#include "physics.h"

#include <box2d/box2d.h>

struct B2Body {
    b2BodyId id = b2_nullBodyId;
};
struct PhysicsEngine {
    b2WorldId world_id = b2_nullWorldId;
};

static auto body_type_for(flecs::entity e) -> b2BodyType {
    if (e.has<Static>()) {
        return b2_staticBody;
    }
    if (e.has<Kinematic>()) {
        return b2_kinematicBody;
    }
    return b2_dynamicBody;
}

static auto shape_def_for(flecs::entity e) -> b2ShapeDef {
    b2ShapeDef sd = b2DefaultShapeDef();

    if (const auto* v = e.try_get<Density>()) {
        sd.density = v->value;
    }
    if (const auto* v = e.try_get<Friction>()) {
        sd.material.friction = v->value;
    }
    if (const auto* v = e.try_get<Restitution>()) {
        sd.material.restitution = v->value;
    }

    sd.isSensor = e.has<Sensor>();
    sd.enableSensorEvents = sd.enableContactEvents = true;

    if (const auto* cl = e.try_get<CollisionLayers>()) {
        sd.filter.categoryBits = cl->memberships;
        sd.filter.maskBits = cl->filter;
    }

    return sd;
}

static auto entity_from_body(const flecs::world& w, b2BodyId body) -> flecs::entity {
    return w.entity(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(b2Body_GetUserData(body))));
}

Physics::Physics(flecs::world& world) {
    world.component<CollisionBox>().member<float>("height").member<float>("width");
    world.component<CollisionRing>().member<float>("radius");
    world.component<VelocityLinear>().member<float>("x").member<float>("y");
    world.component<VelocityAngular>().member<float>("value");
    world.component<ExternalForce>().member<float>("x").member<float>("y");
    world.component<ExternalImpulse>().member<float>("x").member<float>("y");
    world.component<Density>().member<float>("value");
    world.component<Friction>().member<float>("value");
    world.component<Restitution>().member<float>("value");
    world.component<DampingLinear>().member<float>("value");
    world.component<DampingAngular>().member<float>("value");

    world.component<B2Body>();
    world.component<PhysicsEngine>();

    b2WorldDef wd = b2DefaultWorldDef();
    if (const auto* cfg = world.try_get<PhysicsConfig>()) {
        wd.gravity = {.x = cfg->gravity.x, .y = cfg->gravity.y};
    }

    world.set<PhysicsEngine>({.world_id = b2CreateWorld(&wd)});
    world.set<PhysicsEvents>({});

    auto PI = world.entity("physics::Init").add(flecs::Phase).depends_on(flecs::PreUpdate);
    auto PP = world.entity("physics::Pre").add(flecs::Phase).depends_on(PI);
    auto PS = world.entity("physics::Step").add(flecs::Phase).depends_on(PP);
    auto PO = world.entity("physics::Post").add(flecs::Phase).depends_on(PS);

    world.system<const Position, const Rotation>("physics::init").without<B2Body>().with<Dynamic>().or_().with<Static>().or_().with<Kinematic>().kind(PI).each(Physics::init);
    world.observer<const B2Body, const Position, const Rotation>("physics::teleport").with<Teleport>().event(flecs::OnAdd).each(Physics::teleport);
    world.system<const B2Body, const VelocityLinear, const VelocityAngular>("physics::sync").kind(PP).each(Physics::sync);
    world.system<const B2Body, const ExternalForce>("physics::force").kind(PP).each(Physics::force);
    world.system<const B2Body, ExternalImpulse>("physics::impulse").kind(PP).each(Physics::impulse);
    world.system("physics::step").kind(PS).run(Physics::step);
    world.system<const B2Body, Position, Rotation>("physics::transform").kind(PO).multi_threaded().each(Physics::transform);
    world.system<const B2Body, VelocityLinear, VelocityAngular>("physics::velocity").kind(PO).multi_threaded().each(Physics::velocity);
    world.system("physics::event").kind(PO).run(Physics::event);
    world.system<const RequestRaycast>("physics::raycast").without<ResponseRaycast>().kind(PO).each(Physics::raycast);
    world.system<const RequestAreaQuery>("physics::area").without<ResponseAreaQuery>().kind(PO).each(Physics::area);
    world.system<const RequestExplosion>("physics::explosion").without<ResponseExplosion>().kind(PO).each(Physics::explosion);

    world.observer<B2Body>("physics::cleanup").event(flecs::OnRemove).each(Physics::cleanup);
    world.observer<const B2Body, const DampingLinear>("physics::ldamp").event(flecs::OnSet).each(Physics::ldamp);
    world.observer<const B2Body, const DampingAngular>("physics::adamp").event(flecs::OnSet).each(Physics::adamp);
}

void Physics::init(flecs::iter& it, size_t i, const Position& pos, const Rotation& rot) {
    auto& eng = it.world().get_mut<PhysicsEngine>();
    auto e = it.entity(i);

    b2BodyDef def = b2DefaultBodyDef();
    def.type = body_type_for(e);
    def.position = {.x = pos.value.x, .y = pos.value.y};
    def.rotation = b2MakeRot(rot.angle);
    def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(e.id()));
    def.fixedRotation = e.has<RotationFixed>();
    def.isBullet = e.has<CollisionContinuous>();
    if (const auto* v = e.try_get<DampingLinear>()) {
        def.linearDamping = v->value;
    }
    if (const auto* v = e.try_get<DampingAngular>()) {
        def.angularDamping = v->value;
    }

    b2BodyId body = b2CreateBody(eng.world_id, &def);
    if (const auto* c = e.try_get<CollisionBox>()) {
        auto sd = shape_def_for(e);
        b2Polygon poly = b2MakeBox(c->width / 2, c->height / 2);
        b2CreatePolygonShape(body, &sd, &poly);
    }
    if (const auto* c = e.try_get<CollisionRing>()) {
        auto sd = shape_def_for(e);
        b2Circle circ = {.center = {.x = 0, .y = 0}, .radius = c->radius};
        b2CreateCircleShape(body, &sd, &circ);
    }
    if (const auto* v = e.try_get<VelocityLinear>()) {
        b2Body_SetLinearVelocity(body, {.x = v->value.x, .y = v->value.y});
    }
    if (const auto* v = e.try_get<VelocityAngular>()) {
        b2Body_SetAngularVelocity(body, v->value);
    }
    e.set(B2Body{.id = body});
}

void Physics::teleport(flecs::entity e, const B2Body& b, const Position& pos, const Rotation& rot) {
    b2Body_SetTransform(b.id, {.x = pos.value.x, .y = pos.value.y}, b2MakeRot(rot.angle));
    b2Body_SetLinearVelocity(b.id, {.x = 0, .y = 0});
    e.remove<Teleport>();
}

void Physics::sync(const B2Body& b, const VelocityLinear& lv, const VelocityAngular& av) {
    b2Body_SetLinearVelocity(b.id, {.x = lv.value.x, .y = lv.value.y});
    b2Body_SetAngularVelocity(b.id, av.value);
}

void Physics::force(const B2Body& b, const ExternalForce& f) {
    if (f.value.x != 0 || f.value.y != 0) {
        b2Body_ApplyForceToCenter(b.id, {.x = f.value.x, .y = f.value.y}, true);
    }
}

void Physics::impulse(const B2Body& b, ExternalImpulse& imp) {
    if (imp.value.x != 0 || imp.value.y != 0) {
        b2Body_ApplyLinearImpulseToCenter(b.id, {.x = imp.value.x, .y = imp.value.y}, true);
        imp.value = {0, 0};
    }
}

void Physics::step(flecs::iter& it) {
    while (it.next()) {
        auto& eng = it.world().get_mut<PhysicsEngine>();
        const auto* cfg = it.world().try_get<PhysicsConfig>();
        b2World_Step(eng.world_id, it.delta_time(), (cfg != nullptr) ? cfg->sub_steps : 4);
    }
}

void Physics::transform(const B2Body& b, Position& pos, Rotation& rot) {
    b2Vec2 p = b2Body_GetPosition(b.id);
    pos.value = {p.x, p.y};
    rot.angle = b2Rot_GetAngle(b2Body_GetRotation(b.id));
}

void Physics::velocity(const B2Body& b, VelocityLinear& lv, VelocityAngular& av) {
    b2Vec2 v = b2Body_GetLinearVelocity(b.id);
    lv.value = {v.x, v.y};
    av.value = b2Body_GetAngularVelocity(b.id);
}

void Physics::event(flecs::iter& it) {
    while (it.next()) {
        auto& eng = it.world().get_mut<PhysicsEngine>();
        auto& ev = it.world().get_mut<PhysicsEvents>();
        ev.clear();
        auto w = it.world();

        b2ContactEvents contacts = b2World_GetContactEvents(eng.world_id);
        for (int j = 0; j < contacts.beginCount; j++) {
            auto& c = contacts.beginEvents[j];
            ev.contactBegin.push({
                .entity_a = entity_from_body(w, b2Shape_GetBody(c.shapeIdA)),
                .entity_b = entity_from_body(w, b2Shape_GetBody(c.shapeIdB)),
            });
        }
        for (int j = 0; j < contacts.endCount; j++) {
            auto& c = contacts.endEvents[j];
            ev.contactEnd.push({
                .entity_a = entity_from_body(w, b2Shape_GetBody(c.shapeIdA)),
                .entity_b = entity_from_body(w, b2Shape_GetBody(c.shapeIdB)),
            });
        }
        b2SensorEvents sensors = b2World_GetSensorEvents(eng.world_id);
        for (int j = 0; j < sensors.beginCount; j++) {
            auto& s = sensors.beginEvents[j];
            ev.sensorBegin.push({
                .entity_a = entity_from_body(w, b2Shape_GetBody(s.sensorShapeId)),
                .entity_b = entity_from_body(w, b2Shape_GetBody(s.visitorShapeId)),
            });
        }
        for (int j = 0; j < sensors.endCount; j++) {
            auto& s = sensors.endEvents[j];
            ev.sensorEnd.push({
                .entity_a = entity_from_body(w, b2Shape_GetBody(s.sensorShapeId)),
                .entity_b = entity_from_body(w, b2Shape_GetBody(s.visitorShapeId)),
            });
        }
    }
}

void Physics::raycast(flecs::entity e, const RequestRaycast& req) {
    const auto* eng = e.world().try_get<PhysicsEngine>();
    ResponseRaycast result;
    struct Ctx {
        const flecs::world& w;
        ResponseRaycast* r;
    };
    Ctx ctx{.w = e.world(), .r = &result};

    b2Vec2 o = {.x = req.origin.x, .y = req.origin.y};
    b2Vec2 t = {.x = o.x + (req.direction.x * req.range), .y = o.y + (req.direction.y * req.range)};
    b2World_CastRay(
        eng->world_id, o, t, b2DefaultQueryFilter(),
        [](b2ShapeId shape, b2Vec2 pt, b2Vec2 n, float frac, void* ud) -> float {
            auto* c = static_cast<Ctx*>(ud);
            c->r->hits.push({
                .entity = entity_from_body(c->w, b2Shape_GetBody(shape)),
                .point = {pt.x, pt.y},
                .normal = {n.x, n.y},
                .fraction = frac,
            });
            return 1.0F;
        },
        &ctx);
    e.set(result);
}

void Physics::area(flecs::entity e, const RequestAreaQuery& req) {
    const auto* eng = e.world().try_get<PhysicsEngine>();
    ResponseAreaQuery result;
    struct Ctx {
        const flecs::world& w;
        ResponseAreaQuery* r;
        glm::vec2 center;
        float radius;
    };
    Ctx ctx{.w = e.world(), .r = &result, .center = req.center, .radius = req.radius};

    b2AABB aabb = {
        .lowerBound = {.x = req.center.x - req.radius, .y = req.center.y - req.radius},
        .upperBound = {.x = req.center.x + req.radius, .y = req.center.y + req.radius},
    };
    b2World_OverlapAABB(
        eng->world_id, aabb, b2DefaultQueryFilter(),
        [](b2ShapeId shape, void* ud) -> bool {
            auto* c = static_cast<Ctx*>(ud);
            b2BodyId body = b2Shape_GetBody(shape);
            b2Vec2 p = b2Body_GetPosition(body);
            float dist = glm::length(glm::vec2{p.x, p.y} - c->center);
            if (dist <= c->radius) {
                c->r->hits.push({.entity = entity_from_body(c->w, body), .distance = dist});
            }
            return true;
        },
        &ctx);
    e.set(result);
}

void Physics::explosion(flecs::entity e, const RequestExplosion& req) {
    const auto* eng = e.world().try_get<PhysicsEngine>();
    ResponseExplosion result;
    struct RawHit {
        b2BodyId body;
        float dist;
    };
    FixedBuffer<RawHit, 64> raw;
    struct Ctx {
        FixedBuffer<RawHit, 64>* hits;
        glm::vec2 center;
        float radius;
    };
    Ctx ctx{.hits = &raw, .center = req.center, .radius = req.radius};

    b2AABB aabb = {
        .lowerBound = {.x = req.center.x - req.radius, .y = req.center.y - req.radius},
        .upperBound = {.x = req.center.x + req.radius, .y = req.center.y + req.radius},
    };
    b2World_OverlapAABB(
        eng->world_id, aabb, b2DefaultQueryFilter(),
        [](b2ShapeId shape, void* ud) -> bool {
            auto* c = static_cast<Ctx*>(ud);
            b2BodyId body = b2Shape_GetBody(shape);
            b2Vec2 p = b2Body_GetPosition(body);
            float dist = glm::length(glm::vec2{p.x, p.y} - c->center);
            if (dist <= c->radius && dist > 0.01F) {
                c->hits->push({.body = body, .dist = dist});
            }
            return true;
        },
        &ctx);

    for (auto& h : raw) {
        float intensity = 1.0F - (h.dist / req.radius);
        b2Vec2 p = b2Body_GetPosition(h.body);
        glm::vec2 dir = glm::normalize(glm::vec2{p.x, p.y} - req.center);
        if (b2Body_GetType(h.body) == b2_dynamicBody) {
            b2Body_ApplyLinearImpulseToCenter(h.body,
                                              {
                                                  .x = dir.x * req.force * intensity,
                                                  .y = dir.y * req.force * intensity,
                                              },
                                              true);
        }
        result.hits.push({.entity = entity_from_body(e.world(), h.body), .intensity = intensity});
    }
    e.set(result);
}

void Physics::cleanup(B2Body& b) {
    if (b2Body_IsValid(b.id)) {
        b2DestroyBody(b.id);
    }
}
void Physics::ldamp(const B2Body& b, const DampingLinear& d) {
    b2Body_SetLinearDamping(b.id, d.value);
}
void Physics::adamp(const B2Body& b, const DampingAngular& d) {
    b2Body_SetAngularDamping(b.id, d.value);
}
