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
#include "component/script.h"
#include "component/settings.h"
#include "component/world.h"
#include "util/ballistics.h"
#include "util/math.h"
#include "util/movement.h"
#include "util/prediction.h"
#include "util/time.h"
#include "decode.h"
#include "network.h"
#include "protocol.h"
#include "server.h"

struct ClientQueries {
    flecs::query<const NetworkId, const Interpolation, const CollisionBox> pred_tanks;
    flecs::query<Interpolation, Position, Rotation> interp;
    flecs::query<const Dying> dying;
    flecs::query<InputFlags, Position, Rotation, Interpolation> local_tank;
    flecs::query<Position, Rotation, VelocityLinear, Predicted> ghosts;
    flecs::query<const NetworkId, const Position, const Rotation, const CollisionBox> enemies;
    flecs::query<const Position, const Predicted> pred_bullets;
    flecs::query<const NetworkId, Position, Rotation> smooth;
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
    std::vector<Prediction::Tank> tanks;
    world.get<ClientQueries>().pred_tanks.each([&](flecs::entity e, const NetworkId& id, const Interpolation& in, const CollisionBox& box) -> void {
        if (!in.ready) {
            return;
        }
        const auto* dl = e.try_get<DampingLinear>();
        const auto* da = e.try_get<DampingAngular>();
        tanks.push_back({.id = id.value, .pos = in.position, .angle = in.angle, .box = box, .ldamp = dl ? dl->value : 0.0F, .adamp = da ? da->value : 0.0F, .self = id.value == conn.self});
    });
    pred.sync(tanks);

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

void prediction_smooth(const flecs::query<const NetworkId, Position, Rotation>& tanks, const std::unordered_map<uint64_t, Prediction::Pose>& shoved) {
    tanks.each([&](flecs::entity e, const NetworkId& id, Position& p, Rotation& r) -> void {
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

        if (!o->vinit) {
            o->vpos = tp;
            o->vang = ta;
            o->vinit = true;
        }
        if (!present && glm::length(o->vpos - tp) < 0.5F) {
            o->vpos = tp;
            o->vang = ta;
        } else {
            constexpr float k = 0.30F;
            o->vpos += (tp - o->vpos) * k;
            o->vang += math::angle_difference(ta, o->vang) * k;
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
};

void ghosts_advance(flecs::world& world, const ClientQueries& q, const std::vector<Shot>& shots, float dt) {
    for (const auto& s : shots) {
        glm::vec2 fwd = math::heading(s.angle);
        world.entity()
            .set(Position{.value = s.muzzle})
            .set(Rotation{.angle = s.angle})
            .set(VelocityLinear{.value = fwd * s.speed})
            .set(Predicted{.life = s.life, .id = s.prediction})
            .set(Bullet{.speed = s.speed});
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
    uint64_t deadline = conn.newest + static_cast<uint64_t>(std::min(conn.rtt, 1.0) / TICK_DT) + VIEW_MAX + 12;

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
    std::vector<flecs::entity> killed;
    q.pred_bullets.each([&](flecs::entity b, const Position& p, const Predicted& pred) -> void {
        bool pointblank = have_self && glm::distance(p.value, self_pos) < 80.0F;
        const Enemy* best = nullptr;
        float bestd2 = 1e30F;
        for (const auto& e : enemies) {
            bool hit = math::point_in_box(p.value, e.pos, e.angle, e.hw, e.hh);
            if (!hit && pointblank && e.nid != conn.self) {
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
        if (pred.id && best->nid != conn.self) {
            killed.push_back(best->e);
        }
    });
    for (auto b : impacted) {
        if (b.is_alive()) {
            b.destruct();
        }
    }
    for (auto k : killed) {
        if (k.is_alive()) {
            k.set<Dying>({deadline});
        }
    }
}

}

static auto host_peer(flecs::world world) -> flecs::entity {
    flecs::entity tank;
    world.query_builder().with<Local>().with<Tank>().build().each([&](flecs::entity t) -> void { tank = t; });
    flecs::entity peer;
    if (tank.is_alive()) {
        world.query_builder().with<Controls>(tank).build().each([&](flecs::entity p) -> void { peer = p; });
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
        .pred_tanks = world.query_builder<const NetworkId, const Interpolation, const CollisionBox>().with<Tank>().without<Dying>().build(),
        .interp = world.query_builder<Interpolation, Position, Rotation>().with<Remote>().build(),
        .dying = world.query_builder<const Dying>().build(),
        .local_tank = world.query_builder<InputFlags, Position, Rotation, Interpolation>().with<Local>().build(),
        .ghosts = world.query_builder<Position, Rotation, VelocityLinear, Predicted>().build(),
        .enemies = world.query_builder<const NetworkId, const Position, const Rotation, const CollisionBox>().with<Tank>().without<Dying>().build(),
        .pred_bullets = world.query_builder<const Position, const Predicted>().with<Bullet>().build(),
        .smooth = world.query_builder<const NetworkId, Position, Rotation>().with<Tank>().with<Remote>().build(),
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
            if (e.has<Bullet>() && e.has<VelocityLinear>()) {
                glm::vec2 vel = e.get<VelocityLinear>().value;
                const Sample& anchor = in.at(0);
                double age = render - static_cast<double>(anchor.tick);
                if (e.has<Latent>()) {
                    if (age >= -2.0) {
                        e.remove<Latent>();
                    } else {
                        pos.value = anchor.position;
                        rot.angle = in.angle;
                        return;
                    }
                }
                pos.value = anchor.position + vel * static_cast<float>(std::max(age, 0.0) * TICK_DT);
                rot.angle = in.angle;
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
        double lead = std::clamp(BUFFER_TARGET - conn.buffer_avg, -2.0, 3.0);
        uint64_t ctarget = static_cast<uint64_t>(server_now + rtt_ticks + BUFFER_TARGET + lead);
        if ((conn.newest != 0U) && (conn.client_tick + 8 < ctarget || conn.client_tick > ctarget + 8)) {
            conn.client_tick = ctarget;
            conn.last_fire = std::min(conn.last_fire, conn.client_tick);
        }

        {
            std::vector<flecs::entity> revive;
            q.dying.each([&](flecs::entity e, const Dying& d) -> void {
                if (conn.newest >= d.revive) {
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

        q.local_tank.each([&](flecs::entity self, InputFlags& flags, Position& pos, Rotation& rot, Interpolation& interp) -> void {
            uint32_t f = flags.value;
            const auto* ms = self.try_get<MovementStats>();
            MovementStats stats = ms ? *ms : MovementStats{};
            WeaponStats weapon = self.try_get<WeaponStats>() ? *self.try_get<WeaponStats>() : WeaponStats{};
            uint64_t cooldown = weapon.cooldown;

            bool wants_fire = (f & InputFlags::Shoot) != 0 || conn.fire_pending;
            bool advance = !conn.newest || conn.client_tick < ctarget;
            if (wants_fire && !advance) {
                conn.fire_pending = true;
            }
            bool cooled = !conn.newest || conn.client_tick >= conn.last_fire + cooldown;
            bool fire = advance && wants_fire && cooled;
            if (advance && wants_fire) {
                conn.fire_pending = false;
            }
            if (fire) {
                conn.last_fire = conn.client_tick;
                f |= InputFlags::Shoot;
            } else {
                {
                    f &= ~static_cast<uint32_t>(InputFlags::Shoot);
                }
            }
            uint32_t prediction = fire ? ++conn.predictions : 0;
            if (advance) {
                uint64_t target = conn.client_tick++;
                double rtt_half = (std::min(conn.rtt, 1.0) / TICK_DT) * 0.5;
                uint32_t view = static_cast<uint32_t>(std::max<long long>(0, std::llround(rtt_half + conn.delay)));
                view = std::min(view, VIEW_MAX);
                conn.commands.push_back({.tick = target, .flags = f, .prediction = prediction, .view = view, .sent = util::now()});
                while (conn.commands.size() > 256) {
                    conn.commands.erase(conn.commands.begin());
                }
            }

            if (interp.ready && pred.has_self()) {
                const auto& cmds = conn.commands;
                auto velocity = [&](int s, float heading) -> Prediction::Velocity {
                    Prediction::Velocity v;
                    movement::velocity(cmds[s].flags, heading, stats, v.linear, v.angular);
                    return v;
                };
                Prediction::Pose now = pred.run(interp.position, interp.angle, static_cast<int>(cmds.size()), TICK_DT, velocity, true);
                glm::vec2 p = now.pos;
                float a = now.angle;

                if (interp.has_predicted_prev) {
                    Prediction::Pose e = pred.run(
                        interp.predicted_prev, interp.predicted_prev_angle, 1, TICK_DT,
                        [&](int, float heading) -> Prediction::Velocity {
                            Prediction::Velocity v;
                            movement::velocity(f, heading, stats, v.linear, v.angular);
                            return v;
                        },
                        false);
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

                pos.value = p + interp.vis_error;
                rot.angle = a + interp.vis_error_angle;
            } else {
                movement::step(f, stats, pos.value, rot.angle, dt);
            }
            self_pos = pos.value;
            have_self = true;

            if (fire) {
                glm::vec2 fwd = math::heading(rot.angle);
                glm::vec2 muzzle = pos.value + weapon.muzzle * fwd;
                shots.push_back({.muzzle = muzzle, .angle = rot.angle, .speed = weapon.speed, .prediction = prediction, .life = weapon.life});
                if (!conn.commands.empty()) {
                    conn.commands.back().muzzle = muzzle;
                    conn.commands.back().aim = rot.angle;
                }
            }
        });

        if (pred.has_self()) {
            prediction_smooth(q.smooth, pred.shoved());
        }

        ghosts_advance(world, q, shots, dt);
        prediction_hit(q, conn, self_pos, have_self);
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
                cm.flags = c.flags;
                if ((c.flags & InputFlags::Shoot) != 0U) {
                    has_fire = true;
                    cm.prediction = c.prediction;
                    cm.view = c.view;
                    cm.muzzle_x = c.muzzle.x;
                    cm.muzzle_y = c.muzzle.y;
                    cm.aim = c.aim;
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
