#include "client.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "component/asset.h"
#include "component/input.h"
#include "component/interface.h"
#include "component/network.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/render.h"
#include "component/script.h"
#include "component/settings.h"
#include "component/world.h"
#include "util/ballistics.h"
#include "util/controller.h"
#include "util/math.h"
#include "util/prediction.h"
#include "util/time.h"
#include "decode.h"
#include "network.h"
#include "protocol.h"
#include "server.h"

struct ClientQueries {
    flecs::query<const NetworkId, const Interpolation, const CollisionBox> pred_bodies;
    flecs::query<Interpolation, Position, Rotation> interp;
    flecs::query<const Dying> dying;
    flecs::query<InputState, Position, Rotation, Interpolation> local_body;
    flecs::query<Position, Rotation, VelocityLinear, Predicted> ghosts;
    flecs::query<const NetworkId, const Position, const Rotation, const CollisionBox> enemies;
    flecs::query<const Position, const VelocityLinear, const Predicted> pred_bullets;
    flecs::query<const NetworkId, Position, Rotation> smooth;
    flecs::query<const Gravity> gravity;
};

namespace {

auto prediction(flecs::world& world) -> Prediction* {
    if (auto* p = world.try_get_mut<Prediction>()) {
        return p;
    }
    world.add<Prediction>();
    return nullptr;
}

void prediction_sync(flecs::world& world, NetworkConnection& conn, Prediction& pred) {
    glm::vec2 grav{0, 0};
    world.get<ClientQueries>().gravity.each([&](const Gravity& g) -> void { grav = {g.x, g.y}; });
    pred.gravity(grav);

    std::vector<Prediction::Body> bodies;
    world.get<ClientQueries>().pred_bodies.each([&](flecs::entity e, const NetworkId& id, const Interpolation& in, const CollisionBox& box) -> void {
        if (!in.ready) {
            return;
        }
        const auto* dl = e.try_get<DampingLinear>();
        const auto* da = e.try_get<DampingAngular>();
        const auto* gs = e.try_get<GravityScale>();
        bodies.push_back({.id = id.value, .pos = in.position, .angle = in.angle, .box = box, .ldamp = dl ? dl->value : 0.0F, .adamp = da ? da->value : 0.0F, .gravity_scale = gs ? gs->value : 1.0F, .self = id.value == conn.self});
    });
    pred.sync(bodies);

    if (const auto* grid = world.try_get<WorldGrid>(); grid != nullptr && grid->version != conn.tile_version) {
        conn.tile_version = grid->version;
        std::vector<Prediction::StaticBox> boxes;
        std::vector<Prediction::FieldZone> fields;
        if (const auto* tileset = world.try_get<Tileset>()) {
            for (const auto& [key, chunk] : grid->data) {
                for (int y = 0; y < CHUNK_SIZE; ++y) {
                    int x = 0;
                    while (x < CHUNK_SIZE) {
                        const uint16_t id = chunk.tiles[(y * CHUNK_SIZE) + x];
                        if (id == TILE_EMPTY) {
                            ++x;
                            continue;
                        }
                        const TileType& type = tileset->type(id);
                        const float cxf = ((static_cast<float>(chunk.cx) * CHUNK_SIZE) + static_cast<float>(x) + 0.5F) * TILE_SIZE;
                        const float cyf = ((static_cast<float>(chunk.cy) * CHUNK_SIZE) + static_cast<float>(y) + 0.5F) * TILE_SIZE;
                        if (!type.solid) {
                            if (type.drag > 0.0F) {
                                fields.push_back({.center = {cxf, cyf}, .half = {TILE_SIZE * 0.5F, TILE_SIZE * 0.5F}, .drag = type.drag});
                            }
                            ++x;
                            continue;
                        }
                        int x0 = x;
                        while (x < CHUNK_SIZE && chunk.tiles[(y * CHUNK_SIZE) + x] == id) {
                            ++x;
                        }
                        const float len = static_cast<float>(x - x0);
                        boxes.push_back({
                            .center = {((static_cast<float>(chunk.cx) * CHUNK_SIZE) + static_cast<float>(x0) + (len * 0.5F)) * TILE_SIZE, ((static_cast<float>(chunk.cy) * CHUNK_SIZE) + static_cast<float>(y) + 0.5F) * TILE_SIZE},
                            .half = {len * TILE_SIZE * 0.5F, TILE_SIZE * 0.5F},
                            .restitution = type.restitution,
                            .friction = type.friction,
                        });
                    }
                }
            }
        }
        pred.boxes(boxes);
        pred.zones(fields);
    }
}

struct Smooth {
    glm::vec2 vpos{};
    float vang = 0;
    bool vinit = false;
    glm::vec2 shoved_pos{};
    float shoved_angle = 0;
    int contact = 0;
};

void prediction_smooth(const flecs::query<const NetworkId, Position, Rotation>& bodies, const std::unordered_map<uint64_t, Prediction::Pose>& shoved) {
    bodies.each([&](flecs::entity e, const NetworkId& id, Position& p, Rotation& r) -> void {
        auto* o = e.try_get_mut<Smooth>();
        if (!o) {
            e.add<Smooth>();
            return;
        }

        auto sh = shoved.find(id.value);
        if (sh != shoved.end()) {
            o->shoved_pos = sh->second.pos;
            o->shoved_angle = sh->second.angle;
            o->contact = 10;
        } else if (o->contact > 0) {
            {
                o->contact--;
            }
        }
        bool present = o->contact > 0;

        glm::vec2 tp;
        float ta;
        if (present) {
            tp = o->shoved_pos;
            ta = o->shoved_angle;
        } else {
            tp = p.value;
            ta = r.angle;
        }

        if (!o->vinit || e.has<Teleported>()) {
            o->vpos = tp;
            o->vang = ta;
            o->vinit = true;
            e.remove<Teleported>();
        } else if (present) {
            constexpr float k = 0.30F;
            o->vpos += (tp - o->vpos) * k;
            o->vang += math::angle_difference(ta, o->vang) * k;
        } else {
            o->vpos = tp;
            o->vang = ta;
        }
        p.value = o->vpos;
        r.angle = o->vang;
    });
}

struct Shot {
    glm::vec2 muzzle;
    float angle;
    float speed;
    uint32_t prediction;
    float life;
    uint64_t sprite;
    uint64_t sound;
};

void ghosts_advance(flecs::world& world, const ClientQueries& q, const std::vector<Shot>& shots, float dt) {
    for (const auto& s : shots) {
        glm::vec2 fwd = math::heading(s.angle);
        flecs::entity ghost = world.entity()
            .set(Position{.value = s.muzzle})
            .set(Rotation{.angle = s.angle})
            .set(VelocityLinear{.value = fwd * s.speed})
            .set(Predicted{.life = s.life, .id = s.prediction})
            .set(Projectile{.speed = s.speed, .sound = s.sound});
        if (s.sprite != 0) {
            ghost.set(Sprite{.texture = s.sprite});
        } else {
            ghost.set(Sprite{.texture = builtin::DISC, .size = {7.0F, 7.0F}});
        }
    }
    const auto* grid = world.try_get<WorldGrid>();
    const auto* tileset = world.try_get<Tileset>();
    std::vector<flecs::entity> expired;
    q.ghosts.each([&](flecs::entity e, Position& pos, Rotation& rot, VelocityLinear& vel, Predicted& pred) -> void {
        if (grid != nullptr && tileset != nullptr) {
            const TileType* hit = nullptr;
            glm::vec2 hit_at{};
            ballistics::Step r = ballistics::step(*grid, *tileset, pos.value, vel.value, dt, hit, hit_at);
            if (r == ballistics::Step::Absorb) {
                expired.push_back(e);
                return;
            }
            if (r == ballistics::Step::Bounce) {
                rot.angle = std::atan2(vel.value.y, vel.value.x);
            }
        } else {
            pos.value += vel.value * dt;
        }
        pred.life -= dt;
        if (pred.life <= 0) {
            expired.push_back(e);
        }
    });
    for (auto e : expired) {
        if (e.is_alive()) {
            e.destruct();
        }
    }
}

void prediction_hit(const ClientQueries& q, NetworkConnection& conn, glm::vec2 self_pos, bool have_self) {
    struct Enemy {
        flecs::entity e;
        uint64_t nid;
        glm::vec2 pos;
        float angle;
        float hw;
        float hh;
    };
    std::vector<Enemy> enemies;
    q.enemies.each([&](flecs::entity te, const NetworkId& id, const Position& p, const Rotation& r, const CollisionBox& box) -> void {
        enemies.push_back({.e = te, .nid = id.value, .pos = p.value, .angle = r.angle, .hw = (box.width * 0.5F) + HIT_MARGIN_SERVER, .hh = (box.height * 0.5F) + HIT_MARGIN_SERVER});
    });
    if (enemies.empty()) {
        return;
    }

    std::vector<flecs::entity> impacted;
    q.pred_bullets.each([&](flecs::entity b, const Position& p, const VelocityLinear& v, const Predicted& pred) -> void {
        bool pointblank = have_self && glm::distance(p.value, self_pos) < 80.0F;
        glm::vec2 prev = p.value - (v.value * TICK_DT);
        const Enemy* best = nullptr;
        float bestd2 = 1e30F;
        for (const auto& e : enemies) {
            if (e.nid == conn.self) {
                continue;
            }
            bool hit = math::point_in_box(p.value, e.pos, e.angle, e.hw, e.hh);
            for (int k = 1; k <= 4 && !hit; ++k) {
                hit = math::point_in_box(glm::mix(prev, p.value, static_cast<float>(k) / 4.0F), e.pos, e.angle, e.hw, e.hh);
            }
            if (!hit && pointblank) {
                for (int k = 0; k <= 4 && !hit; ++k) {
                    hit = math::point_in_box(glm::mix(self_pos, p.value, static_cast<float>(k) / 4.0F), e.pos, e.angle, e.hw, e.hh);
                }
            }
            if (hit) {
                glm::vec2 d = p.value - e.pos;
                float dd = math::length_squared(d);
                if (dd < bestd2) {
                    bestd2 = dd;
                    best = &e;
                }
            }
        }
        if (!best) {
            return;
        }
        impacted.push_back(b);
    });
    for (auto b : impacted) {
        if (b.is_alive()) {
            b.destruct();
        }
    }
}

}

static auto host_peer(flecs::world world) -> flecs::entity {
    flecs::entity body;
    world.query_builder().with<Local>().build().each([&](flecs::entity t) -> void { body = t; });
    flecs::entity peer;
    if (body.is_alive()) {
        world.query_builder().with<Controls>(body).build().each([&](flecs::entity p) -> void { peer = p; });
    }
    return peer;
}

NetworkClient::NetworkClient(flecs::world& world) {
    world.system("network::client::pump").kind(flecs::OnLoad).immediate().run(NetworkClient::pump);
    world.system("network::client::interpolate").kind(flecs::OnUpdate).immediate().run(NetworkClient::interpolate);
    world.system("network::client::predict").kind(flecs::OnUpdate).immediate().run(NetworkClient::predict);
    world.system("network::client::upload").kind(flecs::OnStore).immediate().run(NetworkClient::upload);

    world.observer<const RequestChat>("network::client::chat").event(flecs::OnSet).each(NetworkClient::chat);

    world.observer<const ResponseAssetAdopt>("network::client::asset").event(flecs::OnSet).each([](flecs::entity e, const ResponseAssetAdopt& r) -> void {
        flecs::world world = e.world();
        if (!r.hashes.empty()) {
            if (auto* conn = world.try_get_mut<NetworkConnection>(); conn != nullptr && (conn->server != nullptr)) {
                MessageAssetRequest req{.hashes = r.hashes};
                serialize::Writer w = wire::message(Message::AssetRequest);
                serialize::encode(w, req);
                wire::send(conn->server, w, CHANNEL_RELIABLE, true);
                SDL_Log("asset: requesting %zu asset(s) from server", r.hashes.size());
            }
        }
        e.destruct();
    });

    world.observer<const RequestViewClick>("network::client::view").event(flecs::OnSet).each([](flecs::entity e, const RequestViewClick& click) -> void {
        flecs::world world = e.world();
        if (auto* conn = world.try_get_mut<NetworkConnection>(); conn != nullptr && conn->connected && conn->server != nullptr) {
            MessageViewEvent ev;
            ev.handler = click.handler;
            for (const auto& [key, value] : click.values) {
                ev.values.push_back({.key = key, .value = value});
            }
            serialize::Writer w = wire::message(Message::ViewEvent);
            serialize::encode(w, ev);
            wire::send(conn->server, w, CHANNEL_RELIABLE, true);
        } else if (world.try_get<NetworkHost>() != nullptr) {
            world.entity().set(RequestViewInteraction{.sender = {.peer = host_peer(world), .name = "host", .admin = true}, .handler = click.handler, .values = click.values});
        }
        e.destruct();
    });

    world.set<ClientQueries>({
        .pred_bodies = world.query_builder<const NetworkId, const Interpolation, const CollisionBox>().with<Controller>().without<Dying>().build(),
        .interp = world.query_builder<Interpolation, Position, Rotation>().with<Remote>().build(),
        .dying = world.query_builder<const Dying>().build(),
        .local_body = world.query_builder<InputState, Position, Rotation, Interpolation>().with<Local>().build(),
        .ghosts = world.query_builder<Position, Rotation, VelocityLinear, Predicted>().build(),
        .enemies = world.query_builder<const NetworkId, const Position, const Rotation, const CollisionBox>().with<Controller>().without<Dying>().build(),
        .pred_bullets = world.query_builder<const Position, const VelocityLinear, const Predicted>().with<Projectile>().build(),
        .smooth = world.query_builder<const NetworkId, Position, Rotation>().with<Controller>().with<Remote>().build(),
        .gravity = world.query_builder<const Gravity>().build(),
    });
}

void NetworkClient::chat(flecs::entity e, const RequestChat& req) {
    flecs::world world = e.world();
    std::string text = req.text;
    if (auto* conn = world.try_get_mut<NetworkConnection>(); (conn != nullptr) && conn->connected && (conn->server != nullptr)) {
        serialize::Writer w = wire::message(Message::Chat);
        MessageChat msg{text};
        serialize::encode(w, msg);
        wire::send(conn->server, w, CHANNEL_RELIABLE, true);
    } else if (world.try_get<NetworkHost>() != nullptr) {
        std::string name;
        if (const Settings* s = world.try_get<Settings>()) {
            name = s->username;
        }
        if (!text.empty() && text[0] == '/') {
            world.entity().set(RequestCommand{.sender = {.peer = host_peer(world), .name = name.empty() ? "host" : name, .admin = true}, .text = text});
        } else {
            world.entity().set(RequestBroadcast{.line = "<" + (name.empty() ? "host" : name) + "> " + text});
        }
    }
    e.destruct();
}

void NetworkClient::teardown(flecs::world& world) {
    world.defer([&] -> void { world.query_builder().with<NetworkId>().or_().with<Predicted>().build().each([](flecs::entity e) -> void { e.destruct(); }); });
    world.remove<Prediction>();
}

void NetworkClient::pump(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto* probe = world.try_get_mut<NetworkConnection>();
        if ((probe == nullptr) || (probe->host == nullptr)) {
            return;
        }
        auto& conn = *probe;

        bool lost = false;
        bool was_connected = false;
        ENetEvent ev;
        while (enet_host_service(conn.host, &ev, 0) > 0) {
            switch (ev.type) {
                case ENET_EVENT_TYPE_CONNECT: {
                    conn.connected = true;
                    SDL_Log("network: connection established");
                    MessageHello hello;
                    if (const Settings* s = world.try_get<Settings>()) {
                        hello.username = s->username;
                    }
                    serialize::Writer w = wire::message(Message::Hello);
                    serialize::encode(w, hello);
                    wire::send(conn.server, w, CHANNEL_RELIABLE, true);
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE:
                    apply_packet(world, conn, ev.packet);
                    enet_packet_destroy(ev.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    was_connected = conn.connected;
                    conn.connected = false;
                    lost = true;
                    SDL_Log("network: disconnected from server");
                    break;
                default:
                    break;
            }
        }

        if (lost) {
            const auto* cs = world.try_get<ConnectionStatus>();
            bool kicked = (cs != nullptr) && cs->state == ConnectionState::Disconnected;
            if (!kicked) {
                world.set<ConnectionStatus>({.state = ConnectionState::Disconnected, .reason = was_connected ? "Connection lost" : "Could not reach server"});
                if (world.has<InterfacePage>()) {
                    world.set(InterfacePage::Status);
                }
            }
            world.entity().add<RequestQuit>();
            return;
        }
    }
}

void NetworkClient::interpolate(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        if (world.try_get<NetworkConnection>() == nullptr) {
            return;
        }
        auto& conn = world.get_mut<NetworkConnection>();

        double server_time = static_cast<double>(conn.newest) + ((util::now() - conn.newest_time) / TICK_DT);
        if (conn.playback == 0) {
            conn.playback = server_time;
        } else {
            conn.playback += (server_time - conn.playback) * 0.1;
        }
        double render = conn.playback - conn.delay;
        world.set<RenderClock>({.tick = render, .now = conn.playback, .valid = conn.newest != 0U});

        auto& dq = conn.despawn_queue;
        for (auto it = dq.begin(); it != dq.end();) {
            if (render >= static_cast<double>(it->second)) {
                auto eit = conn.entities.find(it->first);
                if (eit != conn.entities.end()) {
                    flecs::entity e = world.entity(eit->second);
                    if (e.is_alive()) {
                        e.destruct();
                    }
                    conn.entities.erase(eit);
                }
                it = dq.erase(it);
            } else {
                ++it;
            }
        }

        world.get<ClientQueries>().interp.each([&](flecs::entity e, Interpolation& in, Position& pos, Rotation& rot) -> void {
            if (!in.ready) {
                return;
            }
            if (e.has<Controller>() && in.count >= 2) {
                const Sample& newest = in.at(in.count - 1);
                const Sample& older = in.at(in.count - 2);
                double span = newest.tick - older.tick;
                glm::vec2 vel{0};
                if (const auto* vl = e.try_get<VelocityLinear>()) {
                    vel = vl->value;
                } else if (span > 1e-9) {
                    vel = (newest.position - older.position) / static_cast<float>(span * TICK_DT);
                }
                float avel = span > 1e-9 ? math::angle_difference(newest.angle, older.angle) / static_cast<float>(span * TICK_DT) : 0.0F;
                double ahead = std::clamp(conn.playback - newest.tick, 0.0, 4.0);
                glm::vec2 target = newest.position + (vel * static_cast<float>(ahead * TICK_DT));
                float target_angle = newest.angle + (avel * static_cast<float>(ahead * TICK_DT));

                if (e.has<Teleported>()) {
                    in.vis_error = {0, 0};
                    in.vis_error_angle = 0;
                    in.has_predicted_prev = false;
                }
                if (in.has_predicted_prev) {
                    glm::vec2 expected = in.predicted_prev + (vel * TICK_DT);
                    glm::vec2 jump = target - expected;
                    float jump_angle = math::angle_difference(target_angle, in.predicted_prev_angle + (avel * TICK_DT));
                    if (glm::length(jump) < 48.0F) {
                        in.vis_error -= jump;
                        in.vis_error_angle -= jump_angle;
                    } else {
                        in.vis_error = {0, 0};
                        in.vis_error_angle = 0;
                    }
                }
                in.predicted_prev = target;
                in.predicted_prev_angle = target_angle;
                in.has_predicted_prev = true;

                float decay = std::exp(-TICK_DT / 0.08F);
                in.vis_error *= decay;
                in.vis_error_angle *= decay;
                if (std::fabs(in.vis_error_angle) > 0.6F) {
                    in.vis_error_angle = 0;
                }

                pos.value = target + in.vis_error;
                rot.angle = target_angle + in.vis_error_angle;
                return;
            }
            const Sample* lo = nullptr;
            const Sample* hi = nullptr;
            for (int i = 0; i < in.count; i++) {
                const Sample& s = in.at(i);
                if (s.tick <= render) {
                    {
                        lo = &s;
                    }
                } else {
                    hi = &s;
                    break;
                }
            }
            if (lo && hi) {
                double span = hi->tick - lo->tick;
                float f = (span > 1e-6) ? static_cast<float>(std::clamp((render - lo->tick) / span, 0.0, 1.0)) : 1.0F;
                pos.value = glm::mix(lo->position, hi->position, f);
                rot.angle = lo->angle + (math::angle_difference(hi->angle, lo->angle) * f);
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
        if (world.try_get<NetworkConnection>() == nullptr) {
            return;
        }
        auto& conn = world.get_mut<NetworkConnection>();
        const auto& q = world.get<ClientQueries>();
        float dt = TICK_DT;

        double elapsed = std::min((util::now() - conn.newest_time) / TICK_DT, 15.0);
        double server_now = (conn.newest != 0U) ? static_cast<double>(conn.newest) + elapsed : 0.0;
        double rtt_ticks = std::min(conn.rtt, 1.0) / TICK_DT;
        conn.buffer_avg += (static_cast<double>(conn.buffer) - conn.buffer_avg) * 0.05;
        double pace_gap = (conn.newest != 0U) ? ((server_now + rtt_ticks + BUFFER_TARGET + conn.lead) - static_cast<double>(conn.client_tick)) : 0.0;
        if (std::abs(pace_gap) < 4.0) {
            conn.lead = std::clamp(conn.lead + ((BUFFER_TARGET - conn.buffer_avg) * 0.02), -6.0, 4.0);
        }
        double ctarget_f = server_now + rtt_ticks + BUFFER_TARGET + conn.lead;
        uint64_t ctarget = static_cast<uint64_t>(ctarget_f);
        bool generate = true;
        if (conn.newest != 0U && conn.client_tick + 8 < ctarget) {
            uint64_t floor_tick = conn.commands.empty() ? ctarget : conn.commands.back().tick + 1;
            conn.client_tick = std::max(ctarget, floor_tick);
            conn.last_fire = std::min(conn.last_fire, conn.client_tick);
            ++conn.diagnostics.resyncs;
        } else if (conn.newest != 0U && conn.client_tick > ctarget + 8) {
            generate = false;
            ++conn.diagnostics.stall_ticks;
        }
        if (conn.newest != 0U) {
            double pace_error = ctarget_f - static_cast<double>(conn.client_tick);
            world.set<SimulationClock>({.scale = 1.0 + std::clamp(pace_error * 0.01, -0.02, 0.02)});
        }

        {
            std::vector<flecs::entity> revive;
            q.dying.each([&](flecs::entity e, const Dying& d) -> void {
                if (d.revive != 0 && conn.newest >= d.revive) {
                    revive.push_back(e);
                }
            });
            for (auto e : revive) {
                if (e.is_alive()) {
                    e.remove<Dying>();
                }
            }
        }

        Prediction* predp = prediction(world);
        if (predp == nullptr) {
            return;
        }
        Prediction& pred = *predp;
        prediction_sync(world, conn, pred);

        std::vector<Shot> shots;
        glm::vec2 self_pos{};
        bool have_self = false;

        q.local_body.each([&](flecs::entity self, InputState& in_live, Position& pos, Rotation& rot, Interpolation& interp) -> void {
            if (self.has<Dying>()) {
                if (interp.ready) {
                    pos.value = glm::mix(pos.value, interp.position, 0.35F);
                    rot.angle = interp.angle;
                }
                return;
            }
            const auto* ctrl = self.try_get<Controller>();
            ControlScheme scheme = ctrl != nullptr ? ctrl->scheme : ControlScheme::Differential;
            const auto* ms = self.try_get<DifferentialStats>();
            DifferentialStats dstats{.speed = ms != nullptr ? ms->speed : DifferentialStats{}.speed, .turn = ms != nullptr ? ms->turn : DifferentialStats{}.turn};
            const auto* tds = self.try_get<TopDownStats>();
            TopDownStats tstats = tds != nullptr ? *tds : TopDownStats{};
            const auto* pfs = self.try_get<PlatformerStats>();
            PlatformerStats pstats = pfs != nullptr ? *pfs : PlatformerStats{};
            const auto* selfbox = self.try_get<CollisionBox>();
            float half_w = selfbox != nullptr ? selfbox->width * 0.5F : 0.5F;
            float half_h = selfbox != nullptr ? selfbox->height * 0.5F : 0.5F;
            const auto* grid = world.try_get<WorldGrid>();
            const auto* tileset = world.try_get<Tileset>();
            ProjectileWeapon weapon = self.try_get<ProjectileWeapon>() ? *self.try_get<ProjectileWeapon>() : ProjectileWeapon{};
            uint64_t cooldown = weapon.cooldown;
            glm::vec2 seed_vel{0};
            if (const auto* vl = self.try_get<VelocityLinear>()) {
                seed_vel = vl->value;
            }

            auto step_vel = [&](const glm::vec2& move, float face, glm::vec2 spos, glm::vec2 cur, float heading) -> Prediction::Velocity {
                Prediction::Velocity v;
                InputState si;
                si.move = move;
                if (scheme == ControlScheme::TopDown) {
                    si.aim = math::heading(face);
                    controller::top_down(si, heading, tstats, cur, TICK_DT, v.linear, v.angular);
                } else if (scheme == ControlScheme::Platformer) {
                    bool g = (grid != nullptr && tileset != nullptr) && ballistics::grounded(*grid, *tileset, spos, half_w, half_h);
                    controller::platformer(si, cur, g, pstats, v.linear, v.angular, TICK_DT);
                } else {
                    controller::differential(si, heading, dstats, v.linear, v.angular);
                }
                return v;
            };

            double now_time = util::now();
            if (in_live.down(button::Primary)) {
                conn.fire_pending = true;
                conn.fire_pending_at = now_time;
            }
            bool wants_fire = conn.fire_pending && (now_time - conn.fire_pending_at) < 0.08;
            bool cooled = !conn.newest || conn.client_tick >= conn.last_fire + cooldown;
            const auto* ammo = self.try_get<Ammo>();
            bool has_ammo = ammo == nullptr || (ammo->mag > 0 && ammo->reloading <= 0.0F);
            bool fire = generate && wants_fire && cooled && has_ammo;
            if (fire) {
                conn.fire_pending = false;
                conn.last_fire = conn.client_tick;
            }

            glm::vec2 move = in_live.move;
            float face = glm::dot(in_live.aim, in_live.aim) > 1e-6F ? std::atan2(in_live.aim.y, in_live.aim.x) : rot.angle;
            uint16_t buttons = static_cast<uint16_t>(in_live.buttons & ~button::Primary);
            uint16_t pressed = static_cast<uint16_t>(in_live.pressed & ~button::Primary);
            if (fire) {
                buttons |= button::Primary;
                pressed |= button::Primary;
            }
            uint32_t prediction = fire ? ++conn.predictions : 0;
            if (fire) {
                ++conn.diagnostics.fires;
            }
            int made = 0;
            if (generate) {
                double rtt_half = (std::min(conn.rtt, 1.0) / TICK_DT) * 0.5;
                uint32_t view = static_cast<uint32_t>(std::max<long long>(0, std::llround(rtt_half + BUFFER_TARGET + conn.lead)));
                view = std::min(view, VIEW_MAX);
                uint64_t target = conn.client_tick++;
                conn.commands.push_back({.tick = target, .move = move, .buttons = buttons, .pressed = pressed, .prediction = prediction, .view = view, .sent = util::now(), .face = face});
                made = 1;
                while (conn.commands.size() > 256) {
                    conn.commands.erase(conn.commands.begin());
                }
            }

            if (interp.ready && pred.has_self()) {
                const auto& cmds = conn.commands;
                auto velocity = [&](int s, float heading, glm::vec2 spos, glm::vec2 scur) -> Prediction::Velocity { return step_vel(cmds[s].move, cmds[s].face, spos, scur, heading); };
                Prediction::Pose now = pred.run(interp.position, interp.angle, seed_vel, static_cast<int>(cmds.size()), TICK_DT, velocity, true);
                glm::vec2 p = now.pos;
                float a = now.angle;

                if (interp.has_predicted_prev) {
                    Prediction::Pose e{.pos = interp.predicted_prev, .angle = interp.predicted_prev_angle};
                    if (made > 0 && cmds.size() >= static_cast<size_t>(made)) {
                        auto expected_velocity = [&](int s, float heading, glm::vec2 spos, glm::vec2 scur) -> Prediction::Velocity {
                            const Command& c = cmds[cmds.size() - static_cast<size_t>(made) + static_cast<size_t>(s)];
                            return step_vel(c.move, c.face, spos, scur, heading);
                        };
                        e = pred.run(interp.predicted_prev, interp.predicted_prev_angle, seed_vel, made, TICK_DT, expected_velocity, false);
                    }
                    interp.vis_error += e.pos - p;
                    interp.vis_error_angle += math::angle_difference(e.angle, a);
                    if (glm::length(interp.vis_error) > 120.0F) {
                        interp.vis_error = {0, 0};
                    }
                    if (std::fabs(interp.vis_error_angle) > 0.6F) {
                        interp.vis_error_angle = 0.0F;
                    }
                }
                interp.predicted_prev = p;
                interp.predicted_prev_angle = a;
                interp.has_predicted_prev = true;

                float k = std::exp(-dt / 0.08F);
                interp.vis_error *= k;
                interp.vis_error_angle *= k;
                conn.diagnostics.vis_error_peak = std::max(conn.diagnostics.vis_error_peak, glm::length(interp.vis_error));

                auto& diag_pose = conn.diagnostics;
                if (diag_pose.has_prev_pose) {
                    float step = glm::distance(p, diag_pose.prev_pose);
                    if (step < 64.0F) {
                        diag_pose.pose_step_max = std::max(diag_pose.pose_step_max, step);
                        diag_pose.pose_step_sum += step;
                        ++diag_pose.pose_steps;
                    }
                }
                diag_pose.prev_pose = p;
                diag_pose.has_prev_pose = true;

                pos.value = p + interp.vis_error;
                rot.angle = a + interp.vis_error_angle;
            } else {
                Prediction::Velocity v = step_vel(move, face, pos.value, seed_vel, rot.angle);
                pos.value += v.linear * dt;
                rot.angle += v.angular * dt;
            }
            self_pos = pos.value;
            have_self = true;

            if (fire) {
                glm::vec2 fwd = math::heading(rot.angle);
                glm::vec2 muzzle = pos.value + weapon.muzzle * fwd;
                if (grid != nullptr && tileset != nullptr) {
                    glm::vec2 clear = pos.value;
                    for (int k = 1; k <= 8; ++k) {
                        glm::vec2 pt = glm::mix(pos.value, muzzle, static_cast<float>(k) / 8.0F);
                        if (ballistics::solid(ballistics::tile_at(*grid, *tileset, pt.x, pt.y))) {
                            break;
                        }
                        clear = pt;
                    }
                    muzzle = clear;
                }
                const auto* ps = self.try_get<ProjectileSprite>();
                shots.push_back({.muzzle = muzzle, .angle = rot.angle, .speed = weapon.speed, .prediction = prediction, .life = weapon.life,
                                 .sprite = ps != nullptr ? ps->texture : 0, .sound = weapon.sound});
            }
        });

        if (pred.has_self()) {
            prediction_smooth(q.smooth, pred.shoved());
        }

        ghosts_advance(world, q, shots, dt);
        prediction_hit(q, conn, self_pos, have_self);

        double log_now = util::now();
        auto& diag = conn.diagnostics;
        if (diag.last_log == 0 || !world.has<NetworkDiagnose>()) {
            diag.last_log = log_now;
        } else if (log_now - diag.last_log >= 2.0) {
            float pose_avg = diag.pose_steps > 0 ? diag.pose_step_sum / static_cast<float>(diag.pose_steps) : 0.0F;
            SDL_Log("netgraph: rtt=%.1fms jitter=%.2f buffer=%.1f delay=%.1f commands=%zu resyncs=%u fires=%u error_peak=%.1f pose_step_max=%.2f pose_step_avg=%.2f",
                    conn.rtt * 1000.0, conn.jitter, conn.buffer_avg, conn.delay, conn.commands.size(),
                    diag.resyncs, diag.fires, diag.vis_error_peak, diag.pose_step_max, pose_avg);
            diag = {};
            diag.last_log = log_now;
        }
    }
}

void NetworkClient::upload(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto* probe = world.try_get_mut<NetworkConnection>();
        if ((probe == nullptr) || !probe->connected || (probe->server == nullptr) || !probe->welcomed) {
            return;
        }
        auto& conn = *probe;

        if (!conn.commands.empty()) {
            constexpr int INPUT_BATCH = 8;
            const auto& cmds = conn.commands;
            int n = static_cast<int>(cmds.size());
            int count = n < INPUT_BATCH ? n : INPUT_BATCH;

            MessageInput in;
            in.newest_tick = cmds.back().tick;
            in.send_time = util::now();
            bool has_fire = false;
            for (int j = 0; j < count; ++j) {
                const Command& c = cmds[n - 1 - j];
                MessageInputCommand cm;
                cm.tick_delta = static_cast<uint16_t>(in.newest_tick - c.tick);
                cm.move_x = static_cast<int8_t>(std::clamp(std::lround(c.move.x * 127.0F), -127L, 127L));
                cm.move_y = static_cast<int8_t>(std::clamp(std::lround(c.move.y * 127.0F), -127L, 127L));
                cm.buttons = c.buttons;
                cm.pressed = c.pressed;
                cm.face = static_cast<int16_t>(std::clamp(std::lround(c.face / 0.0001F), -32767L, 32767L));
                if ((c.pressed & button::Primary) != 0U) {
                    has_fire = true;
                    cm.prediction = c.prediction;
                    cm.view = c.view;
                }
                in.commands.push_back(cm);
            }

            serialize::Writer w = wire::message(Message::Input);
            serialize::encode(w, in);
            wire::send(conn.server, w, CHANNEL_UNRELIABLE, false);
            (void)has_fire;
        }

        enet_host_flush(conn.host);
    }
}
