#include "util/prediction.h"
#include "util/math.h"

#include <box2d/box2d.h>

#include <cmath>
#include <unordered_set>

struct Prediction::Impl {
    struct Body {
        b2BodyId id;
        glm::vec2 pos;
        float angle;
    };

    bool ready = false;
    bool has_self = false;
    b2WorldId world{};
    b2BodyId self{};
    std::unordered_map<uint64_t, Body> others;
    std::unordered_map<uint64_t, Pose> shoved;
    std::vector<b2BodyId> statics;
    std::vector<FieldZone> fields;

    ~Impl() {
        if (ready) {
            b2DestroyWorld(world);
        }
    }

    void ensure() {
        if (ready) {
            return;
        }
        b2WorldDef wd = b2DefaultWorldDef();
        wd.gravity = {.x = 0, .y = 0};
        world = b2CreateWorld(&wd);
        ready = true;
    }

    void reset() {
        if (ready) {
            b2DestroyWorld(world);
        }
        ready = false;
        has_self = false;
        others.clear();
        shoved.clear();
        statics.clear();
        fields.clear();
    }

    void boxes(std::span<const StaticBox> boxes) {
        ensure();
        for (b2BodyId body : statics) {
            b2DestroyBody(body);
        }
        statics.clear();
        for (const StaticBox& box : boxes) {
            b2BodyDef bd = b2DefaultBodyDef();
            bd.type = b2_staticBody;
            bd.position = {.x = box.center.x, .y = box.center.y};
            b2BodyId body = b2CreateBody(world, &bd);
            b2ShapeDef sd = b2DefaultShapeDef();
            sd.material.restitution = box.restitution;
            sd.material.friction = box.friction;
            b2Polygon poly = b2MakeBox(box.half.x, box.half.y);
            b2CreatePolygonShape(body, &sd, &poly);
            statics.push_back(body);
        }
    }

    void zones(std::span<const FieldZone> zones) {
        fields.assign(zones.begin(), zones.end());
    }

    auto make_body(const CollisionBox& box, glm::vec2 pos, float angle, float ldamp, float adamp) -> b2BodyId {
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = {.x = pos.x, .y = pos.y};
        bd.rotation = b2MakeRot(angle);
        bd.linearDamping = ldamp;
        bd.angularDamping = adamp;
        b2BodyId body = b2CreateBody(world, &bd);
        b2ShapeDef sd = b2DefaultShapeDef();
        b2Polygon poly = b2MakeBox(box.width * 0.5F, box.height * 0.5F);
        b2CreatePolygonShape(body, &sd, &poly);
        return body;
    }

    void sync(std::span<const Tank> tanks) {
        ensure();
        std::unordered_set<uint64_t> live;
        for (const auto& t : tanks) {
            if (t.self) {
                if (!has_self) {
                    self = make_body(t.box, t.pos, t.angle, t.ldamp, t.adamp);
                    has_self = true;
                }
                continue;
            }
            live.insert(t.id);
            auto it = others.find(t.id);
            if (it == others.end()) {
                others[t.id] = {.id = make_body(t.box, t.pos, t.angle, t.ldamp, t.adamp), .pos = t.pos, .angle = t.angle};
            } else {
                it->second.pos = t.pos;
                it->second.angle = t.angle;
            }
        }
        for (auto it = others.begin(); it != others.end();) {
            if (static_cast<unsigned int>(live.contains(it->first)) == 0U) {
                b2DestroyBody(it->second.id);
                it = others.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto run(glm::vec2 self_pos, float self_angle, int steps, float dt, const std::function<Velocity(int, float)>& velocity, bool record_contacts) -> Pose {
        for (auto& [id, o] : others) {
            b2Body_SetTransform(o.id, {.x = o.pos.x, .y = o.pos.y}, b2MakeRot(o.angle));
            b2Body_SetLinearVelocity(o.id, {.x = 0, .y = 0});
            b2Body_SetAngularVelocity(o.id, 0);
        }
        b2Body_SetTransform(self, {.x = self_pos.x, .y = self_pos.y}, b2MakeRot(self_angle));
        b2Body_SetLinearVelocity(self, {.x = 0, .y = 0});
        b2Body_SetAngularVelocity(self, 0);

        float heading = self_angle;
        for (int s = 0; s < steps; ++s) {
            Velocity v = velocity(s, heading);
            if (!fields.empty()) {
                b2Vec2 sp = b2Body_GetPosition(self);
                for (const FieldZone& z : fields) {
                    if (std::fabs(sp.x - z.center.x) <= z.half.x && std::fabs(sp.y - z.center.y) <= z.half.y) {
                        float k = std::exp(-z.drag * dt);
                        v.linear.x *= k;
                        v.linear.y *= k;
                        break;
                    }
                }
            }
            b2Body_SetLinearVelocity(self, {.x = v.linear.x, .y = v.linear.y});
            b2Body_SetAngularVelocity(self, v.angular);
            b2World_Step(world, dt, 4);
            heading = b2Rot_GetAngle(b2Body_GetRotation(self));
        }

        if (record_contacts) {
            shoved.clear();
            for (auto& [id, o] : others) {
                b2Vec2 bp = b2Body_GetPosition(o.id);
                float angle = b2Rot_GetAngle(b2Body_GetRotation(o.id));
                float dx = bp.x - o.pos.x;
                float dy = bp.y - o.pos.y;
                auto da = math::angle_difference(angle, o.angle);
                if ((dx * dx) + (dy * dy) >= 0.04F || std::abs(da) >= 0.02F) {
                    shoved[id] = {.pos = {bp.x, bp.y}, .angle = angle};
                }
            }
        }

        b2Vec2 fp = b2Body_GetPosition(self);
        return {.pos = {fp.x, fp.y}, .angle = b2Rot_GetAngle(b2Body_GetRotation(self))};
    }
};

Prediction::Prediction() : impl(std::make_unique<Impl>()) {}
Prediction::~Prediction() = default;
Prediction::Prediction(Prediction&&) noexcept = default;
auto Prediction::operator=(Prediction&&) noexcept -> Prediction& = default;

auto Prediction::has_self() const -> bool {
    return impl->has_self;
}

void Prediction::sync(std::span<const Tank> tanks) {
    impl->sync(tanks);
}

void Prediction::boxes(std::span<const StaticBox> boxes) {
    impl->boxes(boxes);
}

void Prediction::zones(std::span<const FieldZone> zones) {
    impl->zones(zones);
}

auto Prediction::run(glm::vec2 self_pos, float self_angle, int steps, float dt, const std::function<Velocity(int step, float heading)>& velocity, bool record_contacts) -> Pose {
    return impl->run(self_pos, self_angle, steps, dt, velocity, record_contacts);
}

auto Prediction::shoved() const -> const std::unordered_map<uint64_t, Pose>& {
    return impl->shoved;
}

void Prediction::reset() {
    impl->reset();
}
