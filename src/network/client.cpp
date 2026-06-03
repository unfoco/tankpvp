#include "client.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

#include "component/object.h"
#include "component/network.h"
#include "component/input.h"
#include "component/physics.h"
#include "component/interface.h"

#include "logic/movement.h"
#include "physics/prediction.h"

#include "util/time.h"

#include "network.h"
#include "protocol.h"
#include "decode.h"

struct ClientQueries {
    flecs::query<const NetworkId, const Interpolation, const CollisionBox> pred_tanks;
    flecs::query<Interpolation, Position, Rotation>                        interp;
    flecs::query<const Dying>                                              dying;
    flecs::query<InputFlags, Position, Rotation, Interpolation>            local_tank;
    flecs::query<Position, const VelocityLinear, Predicted>               ghosts;
    flecs::query<const NetworkId, const Position, const Rotation, const CollisionBox> enemies;
    flecs::query<const Position, const Predicted>                          pred_bullets;
    flecs::query<const NetworkId, Position, Rotation>                      smooth;
};

namespace {

Prediction* prediction(flecs::world& world) {
    if (Prediction* p = world.try_get_mut<Prediction>()) return p;
    world.add<Prediction>();
    return nullptr;
}

void prediction_sync(flecs::world& world, NetworkConnection& conn, Prediction& pred) {
    std::vector<Prediction::Tank> tanks;
    world.get<ClientQueries>().pred_tanks.each([&](flecs::entity e, const NetworkId& id, const Interpolation& in, const CollisionBox& box) {
        if (!in.ready) return;
        const DampingLinear*  dl = e.try_get<DampingLinear>();
        const DampingAngular* da = e.try_get<DampingAngular>();
        tanks.push_back({id.value, in.position, in.angle, box,
                         dl ? dl->value : 0.0f, da ? da->value : 0.0f, id.value == conn.self});
    });
    pred.sync(tanks);
}

struct Smooth {
    glm::vec2 vpos{}; float vang = 0; bool vinit = false;
    glm::vec2 shoved_pos{}; float shoved_angle = 0; int contact = 0;
};

void prediction_smooth(const flecs::query<const NetworkId, Position, Rotation>& tanks,
                                const std::unordered_map<uint64_t, Prediction::Pose>& shoved) {
    tanks.each([&](flecs::entity e, const NetworkId& id, Position& p, Rotation& r) {
        Smooth* o = e.try_get_mut<Smooth>();
        if (!o) { e.add<Smooth>(); return; }

        auto sh = shoved.find(id.value);
        if (sh != shoved.end()) { o->shoved_pos = sh->second.pos; o->shoved_angle = sh->second.angle; o->contact = 10; }
        else if (o->contact > 0) o->contact--;
        bool present = o->contact > 0;

        glm::vec2 tp; float ta;
        if (present) { tp = o->shoved_pos; ta = o->shoved_angle; }
        else         { tp = p.value; ta = r.angle; }

        if (!o->vinit) { o->vpos = tp; o->vang = ta; o->vinit = true; }
        if (!present && glm::length(o->vpos - tp) < 0.5f) { o->vpos = tp; o->vang = ta; }
        else {
            constexpr float k = 0.30f;
            o->vpos += (tp - o->vpos) * k;
            o->vang += std::remainder(ta - o->vang, 2 * std::numbers::pi) * k;
        }
        p.value = o->vpos;
        r.angle = o->vang;
    });
}

struct Shot { glm::vec2 muzzle; float angle; uint32_t prediction; float life; };

void ghosts_advance(flecs::world& world, const ClientQueries& q, const std::vector<Shot>& shots, float dt) {
    for (const auto& s : shots) {
        glm::vec2 fwd(std::cos(s.angle), std::sin(s.angle));
        world.entity()
            .set(Position{.value = s.muzzle})
            .set(Rotation{.angle = s.angle})
            .set(VelocityLinear{.value = fwd * BULLET_SPEED})
            .set(Predicted{.life = s.life, .id = s.prediction})
            .add<Bullet>();
    }
    std::vector<flecs::entity> expired;
    q.ghosts.each([&](flecs::entity e, Position& pos, const VelocityLinear& vel, Predicted& pred) {
        pos.value += vel.value * dt;
        pred.life -= dt;
        if (pred.life <= 0) expired.push_back(e);
    });
    for (auto e : expired) e.destruct();
}

void prediction_hit(const ClientQueries& q, NetworkConnection& conn, glm::vec2 self_pos, bool have_self) {
    uint64_t deadline = conn.newest +
        static_cast<uint64_t>(std::min(conn.rtt, 1.0) / TICK_DT) + VIEW_MAX + 12;

    struct Enemy { flecs::entity e; uint64_t nid; glm::vec2 pos; float angle; float hw; float hh; };
    std::vector<Enemy> enemies;
    q.enemies.each([&](flecs::entity te, const NetworkId& id, const Position& p, const Rotation& r, const CollisionBox& box) {
        enemies.push_back({te, id.value, p.value, r.angle, box.width * 0.5f + HIT_MARGIN_SERVER, box.height * 0.5f + HIT_MARGIN_SERVER});
    });
    if (enemies.empty()) return;

    std::vector<flecs::entity> impacted, killed;
    q.pred_bullets.each([&](flecs::entity b, const Position& p, const Predicted& pred) {
        bool pointblank = have_self && glm::distance(p.value, self_pos) < 80.0f;
        const Enemy* best = nullptr; float bestd2 = 1e30f;
        for (const auto& e : enemies) {
            bool hit = point_in_obb(p.value, e.pos, e.angle, e.hw, e.hh);
            if (!hit && pointblank)
                for (int k = 0; k <= 4 && !hit; ++k)
                    hit = point_in_obb(glm::mix(self_pos, p.value, k / 4.0f), e.pos, e.angle, e.hw, e.hh);
            if (hit) {
                glm::vec2 d = p.value - e.pos;
                float dd = d.x * d.x + d.y * d.y;
                if (dd < bestd2) { bestd2 = dd; best = &e; }
            }
        }
        if (!best) return;
        impacted.push_back(b);
        if (pred.id) { killed.push_back(best->e); conn.hits.push_back({pred.id, best->nid, CLAIM_REDUNDANCY}); }
    });
    for (auto b : impacted) if (b.is_alive()) b.destruct();
    for (auto k : killed) if (k.is_alive()) k.set<Dying>({deadline});
}

}

NetworkClient::NetworkClient(flecs::world& world) {
    world.system("network::client::pump").kind(flecs::OnLoad).immediate().run(NetworkClient::pump);
    world.system("network::client::interpolate").kind(flecs::OnUpdate).immediate().run(NetworkClient::interpolate);
    world.system("network::client::predict").kind(flecs::OnUpdate).immediate().run(NetworkClient::predict);
    world.system("network::client::upload").kind(flecs::OnStore).immediate().run(NetworkClient::upload);

    world.set<ClientQueries>({
        .pred_tanks = world.query_builder<const NetworkId, const Interpolation, const CollisionBox>()
                           .with<Tank>().without<Dying>().build(),
        .interp     = world.query_builder<Interpolation, Position, Rotation>().with<Remote>().build(),
        .dying      = world.query_builder<const Dying>().build(),
        .local_tank = world.query_builder<InputFlags, Position, Rotation, Interpolation>().with<Local>().build(),
        .ghosts     = world.query_builder<Position, const VelocityLinear, Predicted>().build(),
        .enemies    = world.query_builder<const NetworkId, const Position, const Rotation, const CollisionBox>()
                           .with<Tank>().with<Remote>().without<Dying>().build(),
        .pred_bullets = world.query_builder<const Position, const Predicted>().with<Bullet>().build(),
        .smooth     = world.query_builder<const NetworkId, Position, Rotation>().with<Tank>().with<Remote>().build(),
    });
}

void NetworkClient::teardown(flecs::world& world) {
    world.defer([&] {
        world.query_builder().with<NetworkId>().or_().with<Predicted>().build()
            .each([](flecs::entity e) { e.destruct(); });
    });
    world.remove<Prediction>();
}

void NetworkClient::pump(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto* probe = world.try_get_mut<NetworkConnection>();
        if (!probe || !probe->host) return;
        auto& conn = *probe;

        bool lost = false;
        ENetEvent ev;
        while (enet_host_service(conn.host, &ev, 0) > 0) {
            switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                conn.connected = true;
                SDL_Log("network: connection established");
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                apply_packet(world, conn, ev.packet);
                enet_packet_destroy(ev.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                conn.connected = false;
                lost = true;
                SDL_Log("network: disconnected from server");
                break;
            default: break;
            }
        }

        if (lost) {
            if (world.has<InterfacePage>()) world.set(InterfacePage::Main);
            world.entity().add<NetworkRequestQuit>();
            return;
        }
    }
}

void NetworkClient::interpolate(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        if (!world.try_get<NetworkConnection>()) return;
        auto& conn = world.get_mut<NetworkConnection>();

        double server_time = static_cast<double>(conn.newest) + (util::now() - conn.newest_time) / TICK_DT;
        if (conn.playback == 0) conn.playback = server_time;
        else conn.playback += (server_time - conn.playback) * 0.1;
        double render = conn.playback - conn.delay;

        auto& dq = conn.despawn_queue;
        for (auto it = dq.begin(); it != dq.end();) {
            if (render >= static_cast<double>(it->second)) {
                auto eit = conn.entities.find(it->first);
                if (eit != conn.entities.end()) {
                    flecs::entity e = world.entity(eit->second);
                    if (e.is_alive()) e.destruct();
                    conn.entities.erase(eit);
                }
                it = dq.erase(it);
            } else ++it;
        }

        world.get<ClientQueries>().interp.each([&](flecs::entity e, Interpolation& in, Position& pos, Rotation& rot) {
            if (!in.ready) return;
            if (e.has<Bullet>() && e.has<VelocityLinear>()) {
                glm::vec2 vel = e.get<VelocityLinear>().value;
                const Owner* o = e.try_get<Owner>();
                if (o && o->peer == conn.peer_id) {
                    pos.value += vel * static_cast<float>(TICK_DT);
                } else {
                    const Sample& anchor = in.at(0);
                    double age = render - static_cast<double>(anchor.tick);
                    if (e.has<Latent>()) {
                        if (age >= -2.0) e.remove<Latent>();
                        else { pos.value = anchor.position; rot.angle = in.angle; return; }
                    }
                    pos.value = anchor.position + vel * static_cast<float>(std::max(age, 0.0) * TICK_DT);
                }
                rot.angle = in.angle;
                return;
            }
            const Sample* lo = nullptr;
            const Sample* hi = nullptr;
            for (int i = 0; i < in.count; i++) {
                const Sample& s = in.at(i);
                if (s.tick <= render) lo = &s;
                else { hi = &s; break; }
            }
            if (lo && hi) {
                double span = hi->tick - lo->tick;
                float f = (span > 1e-6) ? static_cast<float>(std::clamp((render - lo->tick) / span, 0.0, 1.0)) : 1.0f;
                pos.value = glm::mix(lo->position, hi->position, f);
                rot.angle = lo->angle + std::remainder(hi->angle - lo->angle, 2 * std::numbers::pi) * f;
            } else if (lo) {
                pos.value = lo->position;
                rot.angle = lo->angle;
            } else {
                const Sample& o = in.at(0);
                pos.value = o.position;
                rot.angle = o.angle;
            }
        });
    }
}

void NetworkClient::predict(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        if (!world.try_get<NetworkConnection>()) return;
        auto& conn = world.get_mut<NetworkConnection>();
        const ClientQueries& q = world.get<ClientQueries>();
        float dt = TICK_DT;

        double elapsed    = std::min((util::now() - conn.newest_time) / TICK_DT, 15.0);
        double server_now = conn.newest ? static_cast<double>(conn.newest) + elapsed : 0.0;
        double rtt_ticks  = std::min(conn.rtt, 1.0) / TICK_DT;
        uint64_t ctarget  = static_cast<uint64_t>(server_now + rtt_ticks) + static_cast<uint64_t>(BUFFER_TARGET);
        if (conn.newest && (conn.client_tick + 8 < ctarget || conn.client_tick > ctarget + 8))
            conn.client_tick = ctarget;

        {
            std::vector<flecs::entity> revive;
            q.dying.each([&](flecs::entity e, const Dying& d) { if (conn.newest >= d.revive_tick) revive.push_back(e); });
            for (auto e : revive) if (e.is_alive()) e.remove<Dying>();
        }

        Prediction* predp = prediction(world);
        if (!predp) return;
        Prediction& pred = *predp;
        prediction_sync(world, conn, pred);

        std::vector<Shot> shots;
        glm::vec2 self_pos{};
        bool      have_self = false;

        q.local_tank.each([&](InputFlags& flags, Position& pos, Rotation& rot, Interpolation& interp) {
            uint32_t f = flags.value;

            bool wants_fire = (f & InputFlags::Shoot) != 0 || conn.fire_pending;
            bool advance = !conn.newest || conn.client_tick < ctarget;
            if (wants_fire && !advance) conn.fire_pending = true;
            bool cooled = !conn.newest || conn.client_tick >= conn.last_fire + FIRE_COOLDOWN;
            bool fire = advance && wants_fire && cooled;
            if (advance && wants_fire) conn.fire_pending = false;
            if (fire) { conn.last_fire = conn.client_tick; f |= InputFlags::Shoot; }
            else f &= ~static_cast<uint32_t>(InputFlags::Shoot);
            uint32_t prediction = fire ? ++conn.predictions : 0;
            if (advance) {
                uint64_t target = conn.client_tick++;
                long long render = std::llround(conn.playback - conn.delay);
                uint32_t view = render > 0 && (long long)target > render
                              ? static_cast<uint32_t>((long long)target - render) : 0;
                if (view > VIEW_MAX) view = VIEW_MAX;
                if (conn.commands.size() < 256) conn.commands.push_back({target, f, prediction, view, util::now()});
            }

            if (interp.ready && pred.has_self()) {
                const auto& cmds = conn.commands;
                auto velocity = [&](int s, float heading) {
                    Prediction::Velocity v;
                    tank_velocity(cmds[s].flags, heading, v.linear, v.angular);
                    return v;
                };
                Prediction::Pose now =
                    pred.run(interp.position, interp.angle, (int)cmds.size(), TICK_DT, velocity, true);
                glm::vec2 p = now.pos;
                float     a = now.angle;

                if (interp.has_predicted_prev) {
                    Prediction::Pose e = pred.run(
                        interp.predicted_prev, interp.predicted_prev_angle, 1, TICK_DT,
                        [&](int, float heading) {
                            Prediction::Velocity v;
                            tank_velocity(f, heading, v.linear, v.angular);
                            return v;
                        }, false);
                    interp.vis_error       += e.pos - p;
                    interp.vis_error_angle += std::remainder(e.angle - a, 2 * std::numbers::pi);
                    if (glm::length(interp.vis_error) > 200.0f) interp.vis_error = {0, 0};
                }
                interp.predicted_prev       = p;
                interp.predicted_prev_angle = a;
                interp.has_predicted_prev   = true;

                float k = std::exp(-dt / 0.08f);
                interp.vis_error       *= k;
                interp.vis_error_angle *= k;

                pos.value = p + interp.vis_error;
                rot.angle = a + interp.vis_error_angle;
            } else {
                tank_step(f, pos.value, rot.angle, dt);
            }
            self_pos = pos.value; have_self = true;

            if (fire) {
                glm::vec2 fwd(std::cos(rot.angle), std::sin(rot.angle));
                glm::vec2 muzzle = pos.value + MUZZLE_OFFSET * fwd;
                shots.push_back({muzzle, rot.angle, prediction, BULLET_LIFE});
                if (!conn.commands.empty()) {
                    conn.commands.back().muzzle = muzzle;
                    conn.commands.back().aim    = rot.angle;
                }
            }
        });

        if (pred.has_self()) prediction_smooth(q.smooth, pred.shoved());

        ghosts_advance(world, q, shots, dt);
        prediction_hit(q, conn, self_pos, have_self);
    }
}

void NetworkClient::upload(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto* probe = world.try_get_mut<NetworkConnection>();
        if (!probe || !probe->connected || !probe->server || !probe->welcomed) return;
        auto& conn = *probe;

        if (!conn.commands.empty()) {
            constexpr int INPUT_BATCH = 3;
            const auto& cmds = conn.commands;
            int n = static_cast<int>(cmds.size());
            int count = n < INPUT_BATCH ? n : INPUT_BATCH;

            MessageInput in;
            in.newest_tick = cmds.back().tick;
            in.send_time  = util::now();
            bool has_fire = false;
            for (int j = 0; j < count; ++j) {
                const Command& c = cmds[n - 1 - j];
                MessageInputCommand cm;
                cm.tick_delta = static_cast<uint16_t>(in.newest_tick - c.tick);
                cm.flags = c.flags;
                if (c.flags & InputFlags::Shoot) {
                    has_fire = true;
                    cm.prediction = c.prediction;
                    cm.view = c.view;
                    cm.muzzle_x = c.muzzle.x; cm.muzzle_y = c.muzzle.y; cm.aim = c.aim;
                }
                in.commands.push_back(cm);
            }

            Writer w = wire::message(Message::Input);
            util::encode(w, in);
            wire::send(conn.server, w, CHANNEL_UNRELIABLE, false);
            if (has_fire) wire::send(conn.server, w, CHANNEL_RELIABLE, true);
        }

        if (!conn.hits.empty()) {
            MessageHit hit;
            for (auto& c : conn.hits) hit.claims.push_back({c.prediction, c.target});
            Writer hw = wire::message(Message::Hit);
            util::encode(hw, hit);
            wire::send(conn.server, hw, CHANNEL_UNRELIABLE, false);
            for (auto& c : conn.hits) c.sends--;
            conn.hits.erase(std::remove_if(conn.hits.begin(), conn.hits.end(),
                            [](const NetworkConnection::Claim& c) { return c.sends <= 0; }), conn.hits.end());
        }

        enet_host_flush(conn.host);
    }
}
