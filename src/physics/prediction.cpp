#include "physics/prediction.h"

#include <cmath>
#include <numbers>
#include <unordered_set>

void Prediction::ensure() {
    if (m_ready) {
        return;
    }
    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = {.x = 0, .y = 0};
    m_world = b2CreateWorld(&wd);
    m_ready = true;
}

void Prediction::reset() {
    if (m_ready) {
        b2DestroyWorld(m_world);
    }
    m_ready = false;
    m_has_self = false;
    m_others.clear();
    m_shoved.clear();
}

auto Prediction::make_body(const CollisionBox& box, glm::vec2 pos, float angle, float ldamp, float adamp) -> b2BodyId {
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = {.x = pos.x, .y = pos.y};
    bd.rotation = b2MakeRot(angle);
    bd.linearDamping = ldamp;
    bd.angularDamping = adamp;
    b2BodyId body = b2CreateBody(m_world, &bd);
    b2ShapeDef sd = b2DefaultShapeDef();
    b2Polygon poly = b2MakeBox(box.width * 0.5F, box.height * 0.5F);
    b2CreatePolygonShape(body, &sd, &poly);
    return body;
}

void Prediction::sync(std::span<const Tank> tanks) {
    ensure();
    std::unordered_set<uint64_t> live;
    for (const auto& t : tanks) {
        if (t.self) {
            if (!m_has_self) {
                m_self = make_body(t.box, t.pos, t.angle, t.ldamp, t.adamp);
                m_has_self = true;
            }
            continue;
        }
        live.insert(t.id);
        auto it = m_others.find(t.id);
        if (it == m_others.end()) {
            {
                m_others[t.id] = {.id = make_body(t.box, t.pos, t.angle, t.ldamp, t.adamp), .pos = t.pos, .angle = t.angle};
            }
        } else {
            it->second.pos = t.pos;
            it->second.angle = t.angle;
        }
    }
    for (auto it = m_others.begin(); it != m_others.end();) {
        if (static_cast<unsigned int>(live.contains(it->first)) == 0U) {
            b2DestroyBody(it->second.id);
            it = m_others.erase(it);
        } else {
            {
                ++it;
            }
        }
    }
}

auto Prediction::run(glm::vec2 self_pos, float self_angle, int steps, float dt, const std::function<Velocity(int, float)>& velocity, bool record_contacts) -> Prediction::Pose {
    for (auto& [id, o] : m_others) {
        b2Body_SetTransform(o.id, {.x = o.pos.x, .y = o.pos.y}, b2MakeRot(o.angle));
        b2Body_SetLinearVelocity(o.id, {.x = 0, .y = 0});
        b2Body_SetAngularVelocity(o.id, 0);
    }
    b2Body_SetTransform(m_self, {.x = self_pos.x, .y = self_pos.y}, b2MakeRot(self_angle));
    b2Body_SetLinearVelocity(m_self, {.x = 0, .y = 0});
    b2Body_SetAngularVelocity(m_self, 0);

    float heading = self_angle;
    for (int s = 0; s < steps; ++s) {
        Velocity v = velocity(s, heading);
        b2Body_SetLinearVelocity(m_self, {.x = v.linear.x, .y = v.linear.y});
        b2Body_SetAngularVelocity(m_self, v.angular);
        b2World_Step(m_world, dt, 4);
        heading = b2Rot_GetAngle(b2Body_GetRotation(m_self));
    }

    if (record_contacts) {
        m_shoved.clear();
        for (auto& [id, o] : m_others) {
            b2Vec2 bp = b2Body_GetPosition(o.id);
            float angle = b2Rot_GetAngle(b2Body_GetRotation(o.id));
            float dx = bp.x - o.pos.x;
            float dy = bp.y - o.pos.y;
            auto da = static_cast<float>(std::remainder(angle - o.angle, 2 * std::numbers::pi));
            if ((dx * dx) + (dy * dy) >= 0.04F || std::abs(da) >= 0.02F) {
                m_shoved[id] = {.pos = {bp.x, bp.y}, .angle = angle};
            }
        }
    }

    b2Vec2 fp = b2Body_GetPosition(m_self);
    return {.pos = {fp.x, fp.y}, .angle = b2Rot_GetAngle(b2Body_GetRotation(m_self))};
}
