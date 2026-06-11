#include "server.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "component/asset.h"
#include "component/audio.h"
#include "component/effect.h"
#include "component/input.h"
#include "component/interface.h"
#include "component/network.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/render.h"
#include "component/script.h"
#include "component/world.h"
#include "util/math.h"

#include "protocol.h"
#include "registry.h"
#include "snapshot.h"


static auto peer_entity(flecs::world& world, ENetPeer* peer) -> flecs::entity {
    if (peer == nullptr || peer->data == nullptr) {
        return {};
    }
    return world.entity(static_cast<flecs::entity_t>(reinterpret_cast<uintptr_t>(peer->data)));
}

static void kick(ENetPeer* peer, const std::string& reason) {
    serialize::Writer w = wire::message(Message::Kick);
    MessageKick msg{reason};
    serialize::encode(w, msg);
    wire::send(peer, w, CHANNEL_RELIABLE, true);
    enet_peer_disconnect_later(peer, 0);
}

static void broadcast_reload(flecs::world& world);

static auto manifest_message(const AssetManifest& manifest) -> MessageManifest {
    MessageManifest mm;
    mm.version = manifest.version;
    mm.entries.reserve(manifest.entries.size());
    for (const auto& e : manifest.entries) {
        mm.entries.push_back({.name = e.name, .hash = e.hash, .kind = static_cast<uint8_t>(e.kind), .size = static_cast<uint32_t>(e.bytes.size())});
    }
    return mm;
}

static void pump_assets(flecs::world& world) {
    const auto* manifest = world.try_get<AssetManifest>();
    if (manifest == nullptr) {
        return;
    }
    world.query_builder<Peer>().build().each([&](Peer& p) -> void {
        if (!p.welcomed || (p.peer == nullptr) || p.asset_queue.empty()) {
            return;
        }
        uint32_t budget = ASSET_SEND_BUDGET;
        while (budget > 0 && !p.asset_queue.empty()) {
            const AssetManifest::Entry* entry = manifest->find(p.asset_queue.front());
            if (entry == nullptr) {
                p.asset_queue.pop_front();
                p.asset_offset = 0;
                continue;
            }
            auto total = static_cast<uint32_t>(entry->bytes.size());
            uint32_t n = std::min({total - p.asset_offset, ASSET_CHUNK_BYTES, budget});
            MessageAssetChunk chunk;
            chunk.hash = entry->hash;
            chunk.offset = p.asset_offset;
            chunk.total = total;
            chunk.bytes.assign(entry->bytes.begin() + p.asset_offset, entry->bytes.begin() + p.asset_offset + n);
            serialize::Writer w = wire::message(Message::AssetChunk);
            serialize::encode(w, chunk);
            wire::send(p.peer, w, CHANNEL_ASSET, true);
            p.asset_offset += n;
            budget -= n;
            if (p.asset_offset >= total) {
                p.asset_queue.pop_front();
                p.asset_offset = 0;
            }
        }
    });
}

static void broadcast_chat(flecs::world& world, const std::string& line) {
    serialize::Writer w = wire::message(Message::Chat);
    MessageChat out{line};
    serialize::encode(w, out);
    world.query_builder<Peer>().build().each([&](const Peer& p) {
        if (p.welcomed && (p.peer != nullptr)) {
            wire::send(p.peer, w, CHANNEL_RELIABLE, true);
        }
    });
    if (ChatLog* log = world.try_get_mut<ChatLog>()) {
        log->push(line);
    }
}

static void send_chat(flecs::world& world, flecs::entity peer_entity, const std::string& line) {
    if (peer_entity && peer_entity.is_alive() && peer_entity.has<Peer>()) {
        const Peer& p = peer_entity.get<Peer>();
        if (p.peer != nullptr) {
            serialize::Writer w = wire::message(Message::Chat);
            MessageChat out{line};
            serialize::encode(w, out);
            wire::send(p.peer, w, CHANNEL_RELIABLE, true);
            return;
        }
    }
    if (ChatLog* log = world.try_get_mut<ChatLog>()) {
        log->push(line);
    }
}

static auto peer_socket(flecs::entity peer_entity) -> ENetPeer* {
    if (peer_entity && peer_entity.is_alive() && peer_entity.has<Peer>()) {
        return peer_entity.get<Peer>().peer;
    }
    return nullptr;
}

static auto to_message_widget(const ViewWidget& w) -> MessageViewWidget {
    MessageViewWidget out;
    out.kind = static_cast<uint8_t>(w.kind);
    out.layout = static_cast<uint8_t>(w.layout);
    out.card = static_cast<uint8_t>(w.card ? 1 : 0);
    out.text = w.text;
    out.handler = w.handler;
    out.bind = w.bind;
    out.number = w.number;
    out.bind_max = w.bind_max;
    out.number_max = w.number_max;
    out.color_r = w.color_r;
    out.color_g = w.color_g;
    out.color_b = w.color_b;
    out.bg_r = w.bg_r;
    out.bg_g = w.bg_g;
    out.bg_b = w.bg_b;
    out.bg_a = w.bg_a;
    out.field = w.field;
    for (const auto& b : w.blips) {
        out.blips.push_back({.x = b.x, .y = b.y, .radius = b.radius, .r = b.r, .g = b.g, .b = b.b, .a = b.a});
    }
    for (const auto& c : w.children) {
        out.children.push_back(to_message_widget(c));
    }
    return out;
}

static void deliver_ui(flecs::world& world, const RequestView& req) {
    if (ENetPeer* socket = peer_socket(req.peer); socket != nullptr) {
        if (req.close) {
            serialize::Writer w = wire::message(Message::ViewClose);
            MessageViewClose msg{.id = req.id};
            serialize::encode(w, msg);
            wire::send(socket, w, CHANNEL_RELIABLE, true);
        } else {
            serialize::Writer w = wire::message(Message::ViewOpen);
            MessageViewOpen msg{.id = req.id, .placement = static_cast<uint8_t>(req.placement), .root = to_message_widget(req.root)};
            serialize::encode(w, msg);
            wire::send(socket, w, CHANNEL_RELIABLE, true);
        }
        return;
    }
    auto* view = world.try_get_mut<ViewState>();
    if (view == nullptr) {
        return;
    }
    std::erase_if(view->views, [&](const ViewActive& s) -> bool { return s.id == req.id; });
    if (!req.close) {
        view->views.push_back({.id = req.id, .placement = req.placement, .root = req.root});
        if (req.placement == ViewPlacement::Center) {
            if (auto* page = world.try_get<InterfacePage>(); page != nullptr && *page == InterfacePage::Chat) {
                world.set(InterfacePage::Ingame);
            }
        }
    }
}

static auto view_center(flecs::entity tank) -> glm::vec2 {
    glm::vec2 c{0};
    if (!tank || !tank.is_alive()) {
        return c;
    }
    if (const auto* p = tank.try_get<Position>()) {
        c = p->value;
    }
    if (const auto* cam = tank.try_get<Camera>()) {
        if (cam->target != 0) {
            flecs::entity t = tank.world().entity(cam->target);
            if (t.is_alive()) {
                if (const auto* tp = t.try_get<Position>()) {
                    c = tp->value;
                }
            }
        } else if (cam->focus_x != 0.0F || cam->focus_y != 0.0F) {
            c = {cam->focus_x, cam->focus_y};
        }
    }
    return c;
}

struct ServerQueries {
    flecs::query<Peer> inputs;
    flecs::query<const Position, const Rotation, History> record;
    flecs::query<const Position, const Owner> bullets;
    flecs::query<const History, const CollisionBox> tanks;
    flecs::query<const NetworkId, const History, const CollisionBox> tanks_by_id;
    flecs::query<> unassigned;
    flecs::query<const NetworkId> replicated;
    flecs::query<Peer, Interest, Replication> peers;
};

NetworkServer::NetworkServer(flecs::world& world) {
    world.system("network::server::pump").kind(flecs::OnLoad).immediate().run(NetworkServer::pump);
    flecs::entity post_physics = world.lookup("physics::Post");
    flecs::entity hits_phase = world.entity("network::Hits").add(flecs::Phase).depends_on(post_physics ? post_physics : world.entity(flecs::PostUpdate));
    world.system("network::server::hits").kind(hits_phase).immediate().run(NetworkServer::hits);
    world.system("network::server::replicate").kind(flecs::OnStore).immediate().run(NetworkServer::replicate);
    world.system("network::server::assets").kind(flecs::OnStore).immediate().run([](flecs::iter& it) -> void {
        flecs::world w = it.world();
        pump_assets(w);
    });

    world.set<ServerClock>({});

    world.observer<const RequestBroadcast>("network::server::chat_broadcast").event(flecs::OnSet).each([](flecs::entity e, const RequestBroadcast& c) -> void {
        flecs::world world = e.world();
        broadcast_chat(world, c.line);
        e.destruct();
    });
    world.observer<const RequestTileBroadcast>("network::server::tile_edit").event(flecs::OnSet).each([](flecs::entity e, const RequestTileBroadcast& ed) -> void {
        flecs::world world = e.world();
        if (world.has<NetworkHost>()) {
            auto [cx, cy] = WorldGrid::chunk_coord(ed.tx, ed.ty);
            int64_t key = WorldGrid::key(cx, cy);
            MessageTileSet sm{.tx = ed.tx, .ty = ed.ty, .id = ed.id};
            serialize::Writer w = wire::message(Message::TileSet);
            serialize::encode(w, sm);
            world.query_builder<Peer>().build().each([&](const Peer& p) -> void {
                if (p.welcomed && (p.peer != nullptr) && p.known_chunks.contains(key)) {
                    wire::send(p.peer, w, CHANNEL_RELIABLE, true);
                }
            });
        }
        e.destruct();
    });

    world.observer<const RequestParticles>("network::server::particles_fx").event(flecs::OnSet).each([](flecs::entity e, const RequestParticles& fx) -> void {
        flecs::world world = e.world();
        if (!world.has<NetworkHost>()) {
            return;
        }
        MessageParticles msg{
            .x = fx.position.x, .y = fx.position.y, .dir = fx.dir, .spread = fx.spread, .count = fx.count, .texture = fx.texture,
            .speed_min = fx.speed_min, .speed_max = fx.speed_max, .size_min = fx.size_min, .size_max = fx.size_max,
            .life_min = fx.life_min, .life_max = fx.life_max, .gravity = fx.gravity, .drag = fx.drag, .spin = fx.spin, .grow = fx.grow,
            .r = fx.r, .g = fx.g, .b = fx.b, .alpha = fx.alpha, .additive = static_cast<uint8_t>(fx.additive ? 1 : 0),
        };
        serialize::Writer w = wire::message(Message::Particles);
        serialize::encode(w, msg);
        world.query_builder<Peer>().build().each([&](const Peer& pr) -> void {
            if (pr.welcomed && (pr.peer != nullptr)) {
                wire::send(pr.peer, w, CHANNEL_RELIABLE, true);
            }
        });
    });

    world.observer<const RequestBurst>("network::server::burst_fx").event(flecs::OnSet).each([](flecs::entity e, const RequestBurst& fx) -> void {
        flecs::world world = e.world();
        MessageEffect msg{.x = fx.x, .y = fx.y, .angle = 0, .r = fx.r, .g = fx.g, .b = fx.b};
        serialize::Writer w = wire::message(Message::Effect);
        serialize::encode(w, msg);
        world.query_builder<Peer>().build().each([&](const Peer& pr) -> void {
            if (pr.welcomed && (pr.peer != nullptr)) {
                wire::send(pr.peer, w, CHANNEL_RELIABLE, true);
            }
        });
        e.destruct();
    });

    world.observer("network::server::death_fx").with<Dying>().event(flecs::OnAdd).each([](flecs::entity e) -> void {
        flecs::world world = e.world();
        if (!world.has<NetworkHost>()) {
            return;
        }
        glm::vec2 dp{0};
        float da = 0;
        glm::vec3 dc{255, 255, 255};
        if (const auto* pp = e.try_get<Position>()) {
            dp = pp->value;
        }
        if (const auto* rr = e.try_get<Rotation>()) {
            da = rr->angle;
        }
        if (const auto* cc = e.try_get<Color>()) {
            dc = cc->value;
        }
        e.set(VelocityLinear{}).set(VelocityAngular{});
        MessageEffect fx{.x = dp.x, .y = dp.y, .angle = da, .r = static_cast<uint8_t>(dc.r), .g = static_cast<uint8_t>(dc.g), .b = static_cast<uint8_t>(dc.b)};
        serialize::Writer w = wire::message(Message::Effect);
        serialize::encode(w, fx);
        world.query_builder<Peer>().build().each([&](const Peer& pr) -> void {
            if (pr.welcomed && (pr.peer != nullptr)) {
                wire::send(pr.peer, w, CHANNEL_RELIABLE, true);
            }
        });
    });

    world.system<Camera>("network::server::camera_focus").kind(flecs::OnUpdate).each([](flecs::entity e, Camera& cam) -> void {
        if (cam.target == 0) {
            return;
        }
        flecs::entity t = e.world().entity(cam.target);
        if (t.is_alive()) {
            if (const auto* p = t.try_get<Position>()) {
                if (cam.focus_x != p->value.x || cam.focus_y != p->value.y) {
                    cam.focus_x = p->value.x;
                    cam.focus_y = p->value.y;
                    e.modified<Camera>();
                }
                return;
            }
        }
        cam.target = 0;
        e.modified<Camera>();
    });

    world.system<const Dying>("network::server::respawn").kind(flecs::OnUpdate).each([](flecs::entity e, const Dying& d) -> void {
        flecs::world world = e.world();
        const auto* host = world.try_get<NetworkHost>();
        if (host == nullptr || d.revive == 0 || host->tick < d.revive) {
            return;
        }
        e.remove<Dying>();
        if (const auto* o = e.try_get<Owner>()) {
            uint32_t pid = o->peer;
            e.set(Position{.value = {300.0F + (static_cast<float>(pid % 8) * 70.0F), 300.0F}}).set(Rotation{.angle = 0}).set(VelocityLinear{}).set(VelocityAngular{}).add<Teleport>();
            if (auto* sp = e.try_get_mut<Spawn>()) {
                sp->epoch++;
            }
        }
    });

    world.system("network::server::interest").interval(0.25).kind(flecs::OnUpdate).run([](flecs::iter& it) -> void {
        flecs::world world = it.world();
        if (!world.has<NetworkHost>()) {
            return;
        }
        auto* grid = world.try_get_mut<WorldGrid>();
        if (grid == nullptr) {
            return;
        }
        constexpr float CHUNK_UNITS = CHUNK_SIZE * TILE_SIZE;
        world.query_builder<Peer>().build().each([&](flecs::entity pe, Peer& p) -> void {
            if (!p.welcomed || p.peer == nullptr) {
                return;
            }
            flecs::entity tank = pe.target<Controls>();
            const auto* pos = tank.is_alive() ? tank.try_get<Position>() : nullptr;
            if (pos == nullptr) {
                return;
            }
            const auto* interest = pe.try_get<Interest>();
            float radius = (interest != nullptr) ? interest->radius : 1200.0F;
            int chunks = std::max(1, static_cast<int>(std::ceil(radius / CHUNK_UNITS)));
            glm::vec2 vc = view_center(tank);
            auto [ccx, ccy] = WorldGrid::chunk_coord(static_cast<int>(std::floor(vc.x / TILE_SIZE)), static_cast<int>(std::floor(vc.y / TILE_SIZE)));

            if (p.grid_wipe != grid->wipe) {
                for (int64_t key : p.known_chunks) {
                    MessageTileUnload um{.cx = static_cast<int32_t>(key >> 32), .cy = static_cast<int32_t>(static_cast<uint32_t>(key))};
                    serialize::Writer uw = wire::message(Message::TileUnload);
                    serialize::encode(uw, um);
                    wire::send(p.peer, uw, CHANNEL_RELIABLE, true);
                }
                p.known_chunks.clear();
                p.grid_wipe = grid->wipe;
            }

            for (auto kit = p.known_chunks.begin(); kit != p.known_chunks.end();) {
                int32_t kcx = static_cast<int32_t>(*kit >> 32);
                int32_t kcy = static_cast<int32_t>(static_cast<uint32_t>(*kit));
                if (std::abs(kcx - ccx) > chunks + 1 || std::abs(kcy - ccy) > chunks + 1) {
                    MessageTileUnload um{.cx = kcx, .cy = kcy};
                    serialize::Writer uw = wire::message(Message::TileUnload);
                    serialize::encode(uw, um);
                    wire::send(p.peer, uw, CHANNEL_RELIABLE, true);
                    kit = p.known_chunks.erase(kit);
                } else {
                    ++kit;
                }
            }
            constexpr int CHUNK_BUDGET = 8;
            int spent = 0;
            for (int dy = -chunks; dy <= chunks && spent < CHUNK_BUDGET; ++dy) {
                for (int dx = -chunks; dx <= chunks && spent < CHUNK_BUDGET; ++dx) {
                    int64_t key = WorldGrid::key(ccx + dx, ccy + dy);
                    if (p.known_chunks.contains(key)) {
                        continue;
                    }
                    auto dit = grid->data.find(key);
                    if (dit == grid->data.end()) {
                        if (grid->generated.insert(key).second) {
                            world.entity().set(RequestGenerateChunk{.cx = ccx + dx, .cy = ccy + dy});
                            ++spent;
                        }
                        continue;
                    }
                    MessageTileChunk cm;
                    cm.cx = dit->second.cx;
                    cm.cy = dit->second.cy;
                    cm.tiles.assign(dit->second.tiles, dit->second.tiles + CHUNK_AREA);
                    serialize::Writer w = wire::message(Message::TileChunk);
                    serialize::encode(w, cm);
                    wire::send(p.peer, w, CHANNEL_RELIABLE, true);
                    p.known_chunks.insert(key);
                    ++spent;
                }
            }
        });
    });

    world.system("network::server::tileset_sync").interval(0.5).kind(flecs::OnUpdate).run([](flecs::iter& it) -> void {
        flecs::world world = it.world();
        static uint16_t last = 0xFFFF;
        const auto* tileset = world.try_get<Tileset>();
        if (!world.has<NetworkHost>() || tileset == nullptr || tileset->version == last) {
            return;
        }
        last = tileset->version;
        MessageTileset tm;
        for (const TileType& t : tileset->types) {
            tm.types.push_back({.texture = t.texture, .solid = static_cast<uint8_t>(t.solid ? 1 : 0), .restitution = t.restitution, .friction = t.friction, .drag = t.drag, .hp = t.hp});
        }
        serialize::Writer tw = wire::message(Message::Tileset);
        serialize::encode(tw, tm);
        world.query_builder<Peer>().build().each([&](const Peer& p) -> void {
            if (p.welcomed && (p.peer != nullptr)) {
                wire::send(p.peer, tw, CHANNEL_RELIABLE, true);
            }
        });
    });
    world.observer().with<RequestReload>().event(flecs::OnAdd).each([](flecs::entity e) -> void {
        flecs::world world = e.world();
        broadcast_reload(world);
        e.destruct();
    });
    world.observer().with<ResponseAssetScan>().event(flecs::OnAdd).each([](flecs::entity e) -> void {
        flecs::world world = e.world();
        if (const auto* manifest = world.try_get<AssetManifest>()) {
            MessageManifest mm = manifest_message(*manifest);
            serialize::Writer mw = wire::message(Message::Manifest);
            serialize::encode(mw, mm);
            world.query_builder<Peer>().build().each([&](const Peer& p) -> void {
                if (p.welcomed && (p.peer != nullptr)) {
                    wire::send(p.peer, mw, CHANNEL_RELIABLE, true);
                }
            });
        }
        e.destruct();
    });
    world.observer<const RequestSound>("network::server::sound").event(flecs::OnSet).each([](flecs::entity e, const RequestSound& s) -> void {
        flecs::world world = e.world();
        MessageSound msg{.asset = s.asset, .x = s.x, .y = s.y, .volume = s.volume, .global = s.global};
        serialize::Writer w = wire::message(Message::Sound);
        serialize::encode(w, msg);
        world.query_builder<Peer>().build().each([&](const Peer& p) -> void {
            if (p.welcomed && (p.peer != nullptr)) {
                wire::send(p.peer, w, CHANNEL_UNRELIABLE, false);
            }
        });
    });
    world.observer<const RequestReply>("network::server::chat_reply").event(flecs::OnSet).each([](flecs::entity e, const RequestReply& c) -> void {
        flecs::world world = e.world();
        send_chat(world, c.peer, c.line);
        e.destruct();
    });
    world.observer<const RequestView>("network::server::view").event(flecs::OnSet).each([](flecs::entity e, const RequestView& req) -> void {
        flecs::world world = e.world();
        deliver_ui(world, req);
        e.destruct();
    });
    world.observer<const RequestKick>("network::server::kick").event(flecs::OnSet).each([](flecs::entity e, const RequestKick& req) -> void {
        if (ENetPeer* socket = peer_socket(req.peer); socket != nullptr) {
            kick(socket, req.reason);
        }
        e.destruct();
    });

    world.set<ServerQueries>({
        .inputs = world.query_builder<Peer>().build(),
        .record = world.query_builder<const Position, const Rotation, History>().with<Tank>().build(),
        .bullets = world.query_builder<const Position, const Owner>().with<Bullet>().build(),
        .tanks = world.query_builder<const History, const CollisionBox>().with<Tank>().without<Dying>().build(),
        .tanks_by_id = world.query_builder<const NetworkId, const History, const CollisionBox>().with<Tank>().without<Dying>().build(),
        .unassigned = world.query_builder().with<Replicated>().without<NetworkId>().build(),
        .replicated = world.query_builder<const NetworkId>().with<Replicated>().build(),
        .peers = world.query_builder<Peer, Interest, Replication>().build(),
    });
}

void NetworkServer::teardown(flecs::world& world) {
    world.defer([&] -> void { world.query_builder().with<Peer>().or_().with<Replicated>().build().each([](flecs::entity e) -> void { e.destruct(); }); });
}

static auto to_message_command(const CommandInfo& info) -> MessageCommandInfo {
    MessageCommandInfo entry;
    entry.name = info.name;
    entry.description = info.description;
    for (const auto& arg : info.arguments) {
        entry.arguments.push_back({.name = arg.name, .type = arg.type, .optional = static_cast<uint8_t>(arg.optional ? 1 : 0), .values = arg.values});
    }
    for (const auto& sub : info.subcommands) {
        entry.subcommands.push_back(to_message_command(sub));
    }
    return entry;
}

static void send_welcome(flecs::world& world, NetworkHost& host, ENetPeer* peer, uint32_t pid, uint64_t entity) {
    MessageWelcome msg;
    msg.protocol = NETWORK_PROTOCOL;
    msg.peer_id = pid;
    msg.controlled_entity = entity;
    msg.tick = host.tick;
    msg.tickrate = host.tickrate;
    msg.registry_version = world.get<NetworkRegistry>().version;
    msg.components = world.get<NetworkRegistry>().describe();

    serialize::Writer w = wire::message(Message::Welcome);
    serialize::encode(w, msg);
    wire::send(peer, w, CHANNEL_RELIABLE, true);

    MessageCommandList list;
    if (const auto* book = world.try_get<CommandBook>()) {
        for (const auto& info : book->commands) {
            list.commands.push_back(to_message_command(info));
        }
    }
    serialize::Writer cw = wire::message(Message::CommandList);
    serialize::encode(cw, list);
    wire::send(peer, cw, CHANNEL_RELIABLE, true);

    if (const auto* manifest = world.try_get<AssetManifest>()) {
        MessageManifest mm = manifest_message(*manifest);
        serialize::Writer mw = wire::message(Message::Manifest);
        serialize::encode(mw, mm);
        wire::send(peer, mw, CHANNEL_RELIABLE, true);
    }

    if (const auto* tileset = world.try_get<Tileset>(); tileset != nullptr && tileset->types.size() > 1) {
        MessageTileset tm;
        for (const TileType& t : tileset->types) {
            tm.types.push_back({.texture = t.texture, .solid = static_cast<uint8_t>(t.solid ? 1 : 0), .restitution = t.restitution, .friction = t.friction, .drag = t.drag, .hp = t.hp});
        }
        serialize::Writer tw = wire::message(Message::Tileset);
        serialize::encode(tw, tm);
        wire::send(peer, tw, CHANNEL_RELIABLE, true);
    }
}

static void broadcast_reload(flecs::world& world) {
    NetworkRegistry rebuilt;
    rebuilt.build(world);
    rebuilt.version = static_cast<uint16_t>(world.get<NetworkRegistry>().version + 1);
    world.set<NetworkRegistry>(std::move(rebuilt));
    const auto& reg = world.get<NetworkRegistry>();

    MessageRegistry rmsg;
    rmsg.registry_version = reg.version;
    rmsg.components = reg.describe();
    serialize::Writer rw = wire::message(Message::Registry);
    serialize::encode(rw, rmsg);

    MessageCommandList list;
    if (const auto* book = world.try_get<CommandBook>()) {
        for (const auto& info : book->commands) {
            list.commands.push_back(to_message_command(info));
        }
    }
    serialize::Writer cw = wire::message(Message::CommandList);
    serialize::encode(cw, list);

    int peers = 0;
    world.query_builder<Peer>().build().each([&](const Peer& p) {
        if (p.welcomed && (p.peer != nullptr)) {
            wire::send(p.peer, rw, CHANNEL_RELIABLE, true);
            wire::send(p.peer, cw, CHANNEL_RELIABLE, true);
            ++peers;
        }
    });
    SDL_Log("network: hot-reload synced registry v%u + commands to %d client(s)", reg.version, peers);

    world.entity().add<RequestAssetScan>();
}

static void on_hello(flecs::world& world, NetworkHost& host, ENetPeer* epeer, const std::string& username) {
    std::string name = username.substr(0, 16);
    if (name.find(' ') != std::string::npos) {
        kick(epeer, "Username cannot contain spaces");
        return;
    }

    flecs::entity existing = peer_entity(world, epeer);
    if (existing && existing.is_alive() && existing.has<Peer>()) {
        existing.get_mut<Peer>().username = name;
        return;
    }

    if (world.count<Peer>() >= MAX_PLAYERS) {
        kick(epeer, "Server full");
        return;
    }

    uint32_t pid = host.next_peer++;

    flecs::entity tank = world.entity()
                             .set(Color{.value = {50 + ((pid * 53) % 205), 50 + ((pid * 97) % 205), 50 + ((pid * 151) % 205)}})
                             .set(Position{.value = {300.0F + (static_cast<float>(pid % 8) * 70.0F), 300.0F}})
                             .set(Rotation{.angle = 0})
                             .set(VelocityLinear{})
                             .set(VelocityAngular{})
                             .set(CollisionBox{.height = 30, .width = 40})
                             .set(MovementStats{})
                             .set(WeaponStats{})
                             .add<InputFlags>()
                             .add<Dynamic>()
                             .add<Tank>()
                             .set(Firing{})
                             .add<History>()
                             .add<ViewLag>()
                             .set(Owner{.peer = pid});
    uint64_t nid = host.next_id++;
    tank.set<NetworkId>({nid}).add<Replicated>();

    flecs::entity pe = world.entity().set<Peer>({.peer = epeer, .id = pid, .username = name}).add<Interest>().add<Replication>();
    pe.add<Controls>(tank);

    epeer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(pe.id()));
    send_welcome(world, host, epeer, pid, nid);
    SDL_Log("network: peer %u ('%s') connected (entity %llu)", pid, name.c_str(), static_cast<unsigned long long>(nid));
}

static void on_disconnect(flecs::world& world, ENetPeer* epeer) {
    flecs::entity pe = peer_entity(world, epeer);
    if (pe && pe.is_alive()) {
        std::string username = pe.has<Peer>() ? pe.get<Peer>().username : std::string();
        world.entity().set(RequestPlayerLeave{.peer = pe, .username = username});
        flecs::entity tank = pe.target<Controls>();
        if (tank && tank.is_alive()) {
            tank.destruct();
        }
        SDL_Log("network: peer %u disconnected", pe.has<Peer>() ? pe.get<Peer>().id : 0);
        pe.destruct();
    }
    epeer->data = nullptr;
}

static void handle_packet(flecs::world& world, ENetPeer* epeer, ENetPacket* packet) {
    serialize::Reader r(packet->data, packet->dataLength);
    auto kind = static_cast<Message>(r.get<uint8_t>());
    if (kind == Message::Hello) {
        auto hello = serialize::decode<MessageHello>(r);
        if (r.valid()) {
            on_hello(world, world.get_mut<NetworkHost>(), epeer, hello.username);
        }
        return;
    }
    flecs::entity pe = peer_entity(world, epeer);
    if (!pe || !pe.is_alive() || !pe.has<Peer>()) {
        return;
    }

    switch (kind) {
        case Message::Input: {
            Peer& p = pe.get_mut<Peer>();
            auto in = serialize::decode<MessageInput>(r);
            if (!r.valid()) {
                break;
            }
            for (const auto& c : in.commands) {
                if (c.tick_delta > in.newest_tick) {
                    continue;
                }
                uint64_t tick = in.newest_tick - c.tick_delta;
                if (tick <= p.consumed || p.inputs.contains(tick)) {
                    continue;
                }
                uint32_t view = c.view > VIEW_MAX ? VIEW_MAX : c.view;
                p.inputs[tick] = {.tick = tick, .flags = c.flags, .prediction = c.prediction, .view = view, .muzzle = {c.muzzle_x, c.muzzle_y}, .aim = c.aim, .sent = in.send_time};
            }
            while (p.inputs.size() > 128) {
                p.inputs.erase(p.inputs.begin());
            }
            break;
        }
        case Message::AssetRequest: {
            Peer& p = pe.get_mut<Peer>();
            auto req = serialize::decode<MessageAssetRequest>(r);
            if (!r.valid()) {
                break;
            }
            const auto* manifest = world.try_get<AssetManifest>();
            for (uint64_t h : req.hashes) {
                if (manifest != nullptr && (manifest->find(h) != nullptr)) {
                    p.asset_queue.push_back(h);
                }
            }
            break;
        }
        case Message::Ack: {
            auto ackMsg = serialize::decode<MessageAcknowledge>(r);
            if (!r.valid()) {
                break;
            }
            uint64_t ack = ackMsg.tick;
            Peer& p = pe.get_mut<Peer>();
            if (!p.welcomed) {
                p.welcomed = true;
                world.entity().set(RequestPlayerJoin{.peer = pe, .username = p.username});
            }
            if (pe.has<Replication>()) {
                auto& repl = pe.get_mut<Replication>();
                for (auto it = repl.pending.begin(); it != repl.pending.end();) {
                    if (it->second <= ack) {
                        repl.acked.insert(it->first);
                        it = repl.pending.erase(it);
                    } else {
                        {
                            ++it;
                        }
                    }
                }
                for (auto it = repl.removing.begin(); it != repl.removing.end();) {
                    if (it->second <= ack) {
                        repl.base.erase(it->first);
                        repl.acked.erase(it->first);
                        repl.priority.erase(it->first);
                        it = repl.removing.erase(it);
                    } else {
                        {
                            ++it;
                        }
                    }
                }
                for (auto& [nid, comps] : repl.base) {
                    for (auto& [cid, b] : comps) {
                        if (b.tick != 0 && b.tick <= ack) {
                            b.acked = b.sent;
                        }
                    }
                }
            }
            break;
        }
        case Message::Chat: {
            auto chat = serialize::decode<MessageChat>(r);
            if (!r.valid()) {
                break;
            }
            const Peer& p = pe.get<Peer>();
            std::string raw = chat.text.substr(0, 200);
            size_t start = raw.find_first_not_of(' ');
            if (start == std::string::npos) {
                break;
            }
            raw = raw.substr(start, raw.find_last_not_of(' ') - start + 1);
            std::string name = p.username.empty() ? ("player" + std::to_string(p.id)) : p.username;
            if (raw[0] == '/') {
                world.entity().set(RequestCommand{.sender = {.peer = pe, .name = name, .admin = false}, .text = raw});
                break;
            }
            broadcast_chat(world, "<" + name + "> " + raw);
            break;
        }
        case Message::ViewEvent: {
            const Peer& p = pe.get<Peer>();
            std::string name = p.username.empty() ? ("player" + std::to_string(p.id)) : p.username;
            auto ev = serialize::decode<MessageViewEvent>(r);
            if (!r.valid()) {
                break;
            }
            std::vector<std::pair<std::string, std::string>> values;
            for (auto& v : ev.values) {
                values.emplace_back(std::move(v.key), std::move(v.value));
            }
            world.entity().set(RequestViewInteraction{.sender = {.peer = pe, .name = name, .admin = false}, .handler = ev.handler, .values = std::move(values)});
            break;
        }
        default:
            break;
    }
}

void NetworkServer::pump(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto* probe = world.try_get_mut<NetworkHost>();
        if ((probe == nullptr) || (probe->host == nullptr)) {
            return;
        }
        uint64_t host_tick = ++probe->tick;
        ENetHost* sock = probe->host;
        world.set<ServerClock>({.tick = host_tick, .running = true});

        ENetEvent ev;
        while (enet_host_service(sock, &ev, 0) > 0) {
            switch (ev.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    handle_packet(world, ev.peer, ev.packet);
                    enet_packet_destroy(ev.packet);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    on_disconnect(world, ev.peer);
                    break;
                default:
                    break;
            }
        }

        world.get<ServerQueries>().inputs.each([&](flecs::entity pe, Peer& p) -> void {
            if (p.peer == nullptr) {
                return;
            }
            flecs::entity tank = pe.target<Controls>();
            if (tank && tank.is_alive()) {
                constexpr size_t CUSHION = 3;
                if (!p.primed && p.inputs.size() < CUSHION) {
                    tank.set<InputFlags>({0});
                    tank.set<Firing>({.prediction = 0, .view = 0});
                } else if (!p.inputs.empty()) {
                    p.primed = true;
                    auto front = p.inputs.begin();
                    const PendingInput& cmd = front->second;
                    uint32_t flags = cmd.flags;
                    bool shoot = (flags & static_cast<uint32_t>(InputFlags::Shoot)) != 0;
                    const auto* ws = tank.try_get<WeaponStats>();
                    uint64_t cooldown = ws ? ws->cooldown : WeaponStats{}.cooldown;
                    if (shoot && host_tick < p.last_fire + cooldown) {
                        flags &= ~static_cast<uint32_t>(InputFlags::Shoot);
                        shoot = false;
                    }
                    if (shoot) {
                        p.last_fire = host_tick;
                    }
                    tank.set<InputFlags>({flags});
                    tank.set<Firing>({.prediction = cmd.prediction, .view = cmd.view, .muzzle = cmd.muzzle, .aim = cmd.aim, .aimed = shoot});
                    tank.set<ViewLag>({cmd.view});
                    p.consumed = cmd.tick;
                    p.stamp = cmd.sent;
                    p.starved = 0;
                    p.inputs.erase(front);
                } else {
                    ++p.starved;
                    uint32_t last = (p.starved <= 2 && tank.has<InputFlags>()) ? tank.get<InputFlags>().value : 0U;
                    tank.set<InputFlags>({last & ~static_cast<uint32_t>(InputFlags::Shoot)});
                    tank.set<Firing>({.prediction = 0, .view = 0});
                }
            }
            p.simulated = host_tick;
            p.buffer = static_cast<uint32_t>(p.inputs.size());
        });
    }
}

void NetworkServer::hits(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        const auto* host = world.try_get<NetworkHost>();
        if (host == nullptr) {
            return;
        }
        uint64_t tick = host->tick;
        const auto& q = world.get<ServerQueries>();

        q.record.each([&](const Position& p, const Rotation& r, History& h) -> void { h.record(tick, p.value, r.angle); });

        std::vector<flecs::entity> dead_bullets;
        struct Kill {
            flecs::entity victim;
            flecs::entity firer;
            glm::vec2 point;
        };
        std::vector<Kill> kills;

        q.bullets.each([&](flecs::entity bullet, const Position& bp, const Owner& o) -> void {
            flecs::entity firer = bullet.parent();
            uint32_t lag = 0;
            if (const auto* vl = bullet.try_get<ViewLag>()) {
                lag = vl->ticks;
            } else if (firer && firer.is_alive()) {
                if (const auto* vl = firer.try_get<ViewLag>()) {
                    lag = vl->ticks;
                }
            }

            uint64_t view = tick > lag ? tick - lag : 0;
            auto in_obb = [&](glm::vec2 p, const TransformSnapshot* s, const CollisionBox& box) -> bool {
                if (!s) {
                    return false;
                }
                glm::vec2 d = p - s->pos;
                if (math::length_squared(d) > 60.0F * 60.0F) {
                    return false;
                }
                return math::point_in_box(p, s->pos, s->rot, (box.width * 0.5F) + HIT_MARGIN_SERVER, (box.height * 0.5F) + HIT_MARGIN_SERVER);
            };
            const Position* fpos = (firer && firer.is_alive()) ? firer.try_get<Position>() : nullptr;
            bool pointblank = false;
            if (fpos) {
                glm::vec2 fd = bp.value - fpos->value;
                pointblank = math::length_squared(fd) < 80.0F * 80.0F;
            }

            flecs::entity victim;
            float vd2 = 1e30F;
            q.tanks.each([&](flecs::entity tank, const History& h, const CollisionBox& box) -> void {
                bool is_self = (tank == firer);
                TransformSnapshot self_now;
                const TransformSnapshot* s = nullptr;
                if (is_self) {
                    if (fpos == nullptr) {
                        return;
                    }
                    const auto* frot = firer.try_get<Rotation>();
                    self_now = {.tick = tick, .pos = fpos->value, .rot = (frot != nullptr) ? frot->angle : 0.0F};
                    s = &self_now;
                } else {
                    s = h.at(view);
                }
                if (!s) {
                    return;
                }
                bool hit = in_obb(bp.value, s, box);
                if (!hit && pointblank && fpos && !is_self) {
                    for (int k = 0; k <= 4 && !hit; ++k) {
                        hit = in_obb(glm::mix(fpos->value, bp.value, static_cast<float>(k) / 4.0F), s, box);
                    }
                }
                if (hit) {
                    glm::vec2 d = bp.value - s->pos;
                    float dd = math::length_squared(d);
                    if (dd < vd2) {
                        vd2 = dd;
                        victim = tank;
                    }
                }
            });
            if (victim) {
                dead_bullets.push_back(bullet);
                kills.push_back({.victim = victim, .firer = firer, .point = bp.value});
            }
        });

        for (auto e : dead_bullets) {
            if (e.is_alive()) {
                e.destruct();
            }
        }
        for (const auto& kill : kills) {
            flecs::entity e = kill.victim;
            if (!e.is_alive()) {
                continue;
            }
            world.entity().set<RequestHit>({.attacker = kill.firer.is_alive() ? kill.firer.id() : 0,
                                            .victim = e.id(),
                                            .x = kill.point.x,
                                            .y = kill.point.y});
            if (!e.has<Owner>() && !e.has<Dying>()) {
                e.destruct();
            }
        }
    }
}

void NetworkServer::replicate(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        if (world.try_get<NetworkHost>() == nullptr) {
            return;
        }
        auto& host = world.get_mut<NetworkHost>();
        const auto& reg = world.get<NetworkRegistry>();
        const auto& q = world.get<ServerQueries>();

        std::vector<flecs::entity> fresh;
        q.unassigned.run([&](flecs::iter& i) -> void {
            while (i.next()) {
                for (auto k : i) {
                    fresh.push_back(i.entity(k));
                }
            }
        });
        for (auto e : fresh) {
            e.set<NetworkId>({host.next_id++});
        }

        constexpr float CELL = 512.0F;
        auto cell = [](int cx, int cy) -> int64_t { return (static_cast<int64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cy); };
        struct Entry {
            uint64_t nid;
            flecs::entity_t e;
            glm::vec2 pos;
            uint32_t bulletOwner;
        };
        std::unordered_map<int64_t, std::vector<Entry>> grid;
        std::vector<Entry> global;

        q.replicated.each([&](flecs::entity e, const NetworkId& nid) -> void {
            const auto* p = e.try_get<Position>();
            if (p == nullptr) {
                global.push_back({.nid = nid.value, .e = e.id(), .pos = {0, 0}, .bulletOwner = 0});
                return;
            }
            glm::vec2 pos = p->value;
            uint32_t bulletOwner = 0;
            if (e.has<Bullet>()) {
                if (const auto* o = e.try_get<Owner>()) {
                    bulletOwner = o->peer + 1;
                }
            }
            grid[cell(static_cast<int>(std::floor(pos.x / CELL)), static_cast<int>(std::floor(pos.y / CELL)))].push_back(
                {.nid = nid.value, .e = e.id(), .pos = pos, .bulletOwner = bulletOwner});
        });

        q.peers.each([&](flecs::entity pe, Peer& peer, Interest& interest, Replication& repl) -> void {
            if (!peer.welcomed) {
                return;
            }

            glm::vec2 center = view_center(pe.target<Controls>());

            float keep = interest.radius * 1.25F;
            int reach = static_cast<int>(std::ceil(keep / CELL));
            int pcx = static_cast<int>(std::floor(center.x / CELL));
            int pcy = static_cast<int>(std::floor(center.y / CELL));
            float r2 = interest.radius * interest.radius;
            float keep2 = keep * keep;

            std::unordered_map<uint64_t, flecs::entity_t> relevant;
            for (int cy = pcy - reach; cy <= pcy + reach; ++cy) {
                for (int cx = pcx - reach; cx <= pcx + reach; ++cx) {
                    auto cit = grid.find(cell(cx, cy));
                    if (cit == grid.end()) {
                        continue;
                    }
                    for (const auto& en : cit->second) {
                        if (en.bulletOwner == peer.id + 1) {
                            continue;
                        }
                        glm::vec2 d = en.pos - center;
                        float d2 = math::length_squared(d);
                        bool tracked = repl.acked.contains(en.nid) || repl.pending.contains(en.nid);
                        if (d2 <= r2 || (tracked && d2 <= keep2)) {
                            relevant[en.nid] = en.e;
                        }
                    }
                }
            }

            for (const auto& en : global) {
                relevant[en.nid] = en.e;
            }

            uint64_t self_nid = 0;
            if (flecs::entity own = pe.target<Controls>(); own.is_alive()) {
                if (const auto* nid = own.try_get<NetworkId>()) {
                    relevant[nid->value] = own;
                    self_nid = nid->value;
                }
            }

            send_snapshot(world, reg, host, center, peer, repl, relevant, self_nid);
        });

        enet_host_flush(host.host);
    }
}
