#include "physics.h"
#include <box2d/box2d.h>

struct B2Body { b2BodyId id = b2_nullBodyId; };
struct PhysicsEngine { b2WorldId world_id = b2_nullWorldId; };

static b2BodyType body_type_for(flecs::entity e) {
    if (e.has<Static>()) return b2_staticBody;
    if (e.has<Kinematic>()) return b2_kinematicBody;
    return b2_dynamicBody;
}

static b2ShapeDef shape_def_for(flecs::entity e) {
    b2ShapeDef sd = b2DefaultShapeDef();

    if (auto* v = e.try_get<Density>()) sd.density = v->value;
    if (auto* v = e.try_get<Friction>()) sd.material.friction = v->value;
    if (auto* v = e.try_get<Restitution>()) sd.material.restitution = v->value;

    sd.isSensor = e.has<Sensor>();
    sd.enableSensorEvents = sd.enableContactEvents = true;

    if (auto* cl = e.try_get<CollisionLayers>()) {
        sd.filter.categoryBits = cl->memberships;
        sd.filter.maskBits     = cl->filter;
    }

    return sd;
}

static flecs::entity entity_from_body(const flecs::world& w, b2BodyId body) {
    return w.entity((uint64_t)(uintptr_t)b2Body_GetUserData(body));
}

Physics::Physics(flecs::world& world) {
    world.component<B2Body>();
    world.component<PhysicsEngine>();

    b2WorldDef wd = b2DefaultWorldDef();
    if (auto* cfg = world.try_get<PhysicsConfig>()) {
        wd.gravity = {cfg->gravity.x, cfg->gravity.y};
    }

    world.set<PhysicsEngine>({.world_id = b2CreateWorld(&wd)});
    world.set<PhysicsEvents>({});

    auto PI = world.entity("physics::Init").add(flecs::Phase).depends_on(flecs::PreUpdate);
    auto PP = world.entity("physics::Pre").add(flecs::Phase).depends_on(PI);
    auto PS = world.entity("physics::Step").add(flecs::Phase).depends_on(PP);
    auto PO = world.entity("physics::Post").add(flecs::Phase).depends_on(PS);

    world.system<const Position, const Rotation>("physics::init")
        .without<B2Body>().with<Dynamic>().or_().with<Static>().or_().with<Kinematic>()
        .kind(PI).each(Physics::init);
    world.system<const B2Body, const Position, const Rotation>("physics::teleport")
        .with<Teleport>().kind(PP).each(Physics::teleport);
    world.system<const B2Body, const LinearVelocity, const AngularVelocity>("physics::sync")
        .kind(PP).each(Physics::sync);
    world.system<const B2Body, const ExternalForce>("physics::force")
        .kind(PP).each(Physics::force);
    world.system<const B2Body, ExternalImpulse>("physics::impulse")
        .kind(PP).each(Physics::impulse);
    world.system("physics::step")
        .kind(PS).run(Physics::step);
    world.system<const B2Body, Position, Rotation>("physics::transform")
        .kind(PO).multi_threaded().each(Physics::transform);
    world.system<const B2Body, LinearVelocity, AngularVelocity>("physics::velocity")
        .kind(PO).multi_threaded().each(Physics::velocity);
    world.system("physics::event")
        .kind(PO).run(Physics::event);
    world.system<const RaycastRequest>("physics::raycast")
        .without<RaycastResult>().kind(PO).each(Physics::raycast);
    world.system<const AreaQueryRequest>("physics::area")
        .without<AreaQueryResult>().kind(PO).each(Physics::area);
    world.system<const ExplosionRequest>("physics::explosion")
        .without<ExplosionResult>().kind(PO).each(Physics::explosion);

    world.observer<B2Body>("physics::cleanup").event(flecs::OnRemove).each(Physics::cleanup);
    world.observer<const B2Body, const LinearDamping>("physics::ldamp").event(flecs::OnSet).each(Physics::ldamp);
    world.observer<const B2Body, const AngularDamping>("physics::adamp").event(flecs::OnSet).each(Physics::adamp);
}

void Physics::init(flecs::iter& it, size_t i, const Position& pos, const Rotation& rot) {
    auto& eng = it.world().get_mut<PhysicsEngine>();
    auto e = it.entity(i);

    b2BodyDef def = b2DefaultBodyDef();
    def.type = body_type_for(e);
    def.position = {pos.value.x, pos.value.y};
    def.rotation = b2MakeRot(rot.angle);
    def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(e.id()));
    def.fixedRotation = e.has<FixedRotation>();
    def.isBullet = e.has<ContinuousCollision>();
    if (auto* v = e.try_get<LinearDamping>()) def.linearDamping  = v->value;
    if (auto* v = e.try_get<AngularDamping>()) def.angularDamping = v->value;

    b2BodyId body = b2CreateBody(eng.world_id, &def);
    if (auto* c = e.try_get<ColliderBox>()) {
        auto sd = shape_def_for(e); b2Polygon poly = b2MakeBox(c->half_width, c->half_height);
        b2CreatePolygonShape(body, &sd, &poly);
    }
    if (auto* c = e.try_get<ColliderRing>()) {
        auto sd = shape_def_for(e); b2Circle circ = {{0, 0}, c->radius};
        b2CreateCircleShape(body, &sd, &circ);
    }
    if (auto* v = e.try_get<LinearVelocity>())  b2Body_SetLinearVelocity(body, {v->value.x, v->value.y});
    if (auto* v = e.try_get<AngularVelocity>()) b2Body_SetAngularVelocity(body, v->value);
    e.set(B2Body{.id = body});
}

void Physics::teleport(flecs::entity e, const B2Body& b, const Position& pos, const Rotation& rot) {
    b2Body_SetTransform(b.id, {pos.value.x, pos.value.y}, b2MakeRot(rot.angle));
    e.remove<Teleport>();
}

void Physics::sync(const B2Body& b, const LinearVelocity& lv, const AngularVelocity& av) {
    b2Body_SetLinearVelocity(b.id, {lv.value.x, lv.value.y});
    b2Body_SetAngularVelocity(b.id, av.value);
}

void Physics::force(const B2Body& b, const ExternalForce& f) {
    if (f.value.x != 0 || f.value.y != 0) {
        b2Body_ApplyForceToCenter(b.id, {f.value.x, f.value.y}, true);
    }
}

void Physics::impulse(const B2Body& b, ExternalImpulse& imp) {
    if (imp.value.x != 0 || imp.value.y != 0) {
        b2Body_ApplyLinearImpulseToCenter(b.id, {imp.value.x, imp.value.y}, true);
        imp.value = {0, 0};
    }
}

void Physics::step(flecs::iter& it) {
    while (it.next()) {
        auto& eng = it.world().get_mut<PhysicsEngine>();
        auto* cfg = it.world().try_get<PhysicsConfig>();
        b2World_Step(eng.world_id, it.delta_time(), cfg ? cfg->sub_steps : 4);
    }
}

void Physics::transform(const B2Body& b, Position& pos, Rotation& rot) {
    b2Vec2 p  = b2Body_GetPosition(b.id);
    pos.value = {p.x, p.y};
    rot.angle = b2Rot_GetAngle(b2Body_GetRotation(b.id));
}

void Physics::velocity(const B2Body& b, LinearVelocity& lv, AngularVelocity& av) {
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
            ev.contact_begin.push({
                entity_from_body(w, b2Shape_GetBody(c.shapeIdA)),
                entity_from_body(w, b2Shape_GetBody(c.shapeIdB)),
            });
        }
        for (int j = 0; j < contacts.endCount; j++) {
            auto& c = contacts.endEvents[j];
            ev.contact_end.push({
                entity_from_body(w, b2Shape_GetBody(c.shapeIdA)),
                entity_from_body(w, b2Shape_GetBody(c.shapeIdB)),
            });
        }
        b2SensorEvents sensors = b2World_GetSensorEvents(eng.world_id);
        for (int j = 0; j < sensors.beginCount; j++) {
            auto& s = sensors.beginEvents[j];
            ev.sensor_begin.push({
                entity_from_body(w, b2Shape_GetBody(s.sensorShapeId)),
                entity_from_body(w, b2Shape_GetBody(s.visitorShapeId)),
            });
        }
        for (int j = 0; j < sensors.endCount; j++) {
            auto& s = sensors.endEvents[j];
            ev.sensor_end.push({
                entity_from_body(w, b2Shape_GetBody(s.sensorShapeId)),
                entity_from_body(w, b2Shape_GetBody(s.visitorShapeId)),
            });
        }
    }
}

void Physics::raycast(flecs::entity e, const RaycastRequest& req) {
    auto* eng = e.world().try_get<PhysicsEngine>();
    RaycastResult result;
    struct Ctx { const flecs::world& w; RaycastResult* r; };
    Ctx ctx{e.world(), &result};

    b2Vec2 o = {req.origin.x, req.origin.y};
    b2Vec2 t = {o.x + req.direction.x * req.max_dist, o.y + req.direction.y * req.max_dist};
    b2World_CastRay(eng->world_id, o, t, b2DefaultQueryFilter(),
        [](b2ShapeId shape, b2Vec2 pt, b2Vec2 n, float frac, void* ud) -> float {
            auto* c = static_cast<Ctx*>(ud);
            c->r->hits.push({
                entity_from_body(c->w, b2Shape_GetBody(shape)),
                {pt.x, pt.y}, {n.x, n.y}, frac,
            });
            return 1.0f;
        }, &ctx);
    e.set(std::move(result));
}

void Physics::area(flecs::entity e, const AreaQueryRequest& req) {
    auto* eng = e.world().try_get<PhysicsEngine>();
    AreaQueryResult result;
    struct Ctx { const flecs::world& w; AreaQueryResult* r; glm::vec2 center; float radius; };
    Ctx ctx{e.world(), &result, req.center, req.radius};

    b2AABB aabb = {
        {req.center.x - req.radius, req.center.y - req.radius},
        {req.center.x + req.radius, req.center.y + req.radius},
    };
    b2World_OverlapAABB(eng->world_id, aabb, b2DefaultQueryFilter(),
        [](b2ShapeId shape, void* ud) -> bool {
            auto* c = static_cast<Ctx*>(ud);
            b2BodyId body = b2Shape_GetBody(shape);
            b2Vec2 p = b2Body_GetPosition(body);
            float dist = glm::length(glm::vec2{p.x, p.y} - c->center);
            if (dist <= c->radius) {
                c->r->hits.push({entity_from_body(c->w, body), dist});
            }
            return true;
        }, &ctx);
    e.set(std::move(result));
}

void Physics::explosion(flecs::entity e, const ExplosionRequest& req) {
    auto* eng = e.world().try_get<PhysicsEngine>();
    ExplosionResult result;
    struct RawHit { b2BodyId body; float dist; };
    FixedBuffer<RawHit, 64> raw;
    struct Ctx { FixedBuffer<RawHit, 64>* hits; glm::vec2 center; float radius; };
    Ctx ctx{&raw, req.center, req.radius};

    b2AABB aabb = {
        {req.center.x - req.radius, req.center.y - req.radius},
        {req.center.x + req.radius, req.center.y + req.radius},
    };
    b2World_OverlapAABB(eng->world_id, aabb, b2DefaultQueryFilter(),
        [](b2ShapeId shape, void* ud) -> bool {
            auto* c = static_cast<Ctx*>(ud);
            b2BodyId body = b2Shape_GetBody(shape);
            b2Vec2 p = b2Body_GetPosition(body);
            float dist = glm::length(glm::vec2{p.x, p.y} - c->center);
            if (dist <= c->radius && dist > 0.01f) {
                c->hits->push({body, dist});
            }
            return true;
        }, &ctx);

    for (auto& h : raw) {
        float intensity = 1.0f - (h.dist / req.radius);
        b2Vec2 p = b2Body_GetPosition(h.body);
        glm::vec2 dir = glm::normalize(glm::vec2{p.x, p.y} - req.center);
        if (b2Body_GetType(h.body) == b2_dynamicBody) {
            b2Body_ApplyLinearImpulseToCenter(h.body, {
                dir.x * req.force * intensity,
                dir.y * req.force * intensity,
            }, true);
        }
        result.hits.push({
            entity_from_body(e.world(), h.body),
            intensity
        });
    }
    e.set(std::move(result));
}

void Physics::cleanup(B2Body& b) { if (b2Body_IsValid(b.id)) b2DestroyBody(b.id); }
void Physics::ldamp(const B2Body& b, const LinearDamping& d) { b2Body_SetLinearDamping(b.id, d.value); }
void Physics::adamp(const B2Body& b, const AngularDamping& d) { b2Body_SetAngularDamping(b.id, d.value); }
