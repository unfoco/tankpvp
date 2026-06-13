#include "decode.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "component/interface.h"
#include "component/input.h"
#include "component/network.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/script.h"
#include "component/audio.h"
#include "component/render.h"
#include "util/math.h"
#include "util/time.h"
#include "component/asset.h"
#include "component/world.h"
#include "network.h"
#include "protocol.h"
#include "registry.h"

static auto to_view_widget(const MessageViewWidget& w) -> ViewWidget {
    ViewWidget out;
    out.kind = static_cast<ViewKind>(w.kind);
    out.layout = static_cast<ViewLayout>(w.layout);
    out.card = w.card != 0;
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
        out.children.push_back(to_view_widget(c));
    }
    return out;
}

static auto from_message_command(MessageCommandInfo& entry) -> CommandInfo {
    CommandInfo info;
    info.name = std::move(entry.name);
    info.description = std::move(entry.description);
    for (auto& arg : entry.arguments) {
        info.arguments.push_back({.name = std::move(arg.name), .type = std::move(arg.type), .optional = arg.optional != 0, .values = std::move(arg.values)});
    }
    for (auto& sub : entry.subcommands) {
        info.subcommands.push_back(from_message_command(sub));
    }
    return info;
}

static auto mirror(flecs::world& world, NetworkConnection& conn, uint64_t nid) -> flecs::entity {
    auto it = conn.entities.find(nid);
    if (it != conn.entities.end()) {
        flecs::entity e = world.entity(it->second);
        if (e.is_alive()) {
            return e;
        }
        conn.entities.erase(it);
    }
    flecs::entity e = world.entity().set<NetworkId>({nid});
    if (nid == conn.self) {
        e.add<Local>().add<InputFlags>().set<Interpolation>({});
    } else {
        e.add<Remote>().set<Interpolation>({});
    }
    conn.entities[nid] = e.id();
    return e;
}

static void apply_components(flecs::world& world, const NetworkRegistry& reg, NetworkConnection& conn, flecs::entity e, const std::vector<MessageComponentData>& comps, uint64_t tick) {
    bool got_pos = false;
    bool got_rot = false;
    bool is_bullet = false;
    glm::vec2 pos{0};
    float rot = 0;
    bool had_spawn = e.has<Spawn>();
    uint16_t old_epoch = had_spawn ? e.get<Spawn>().epoch : 0;

    for (const auto& cb : comps) {
        auto rit = conn.remap.find(cb.server_id);
        const NetworkRegistry::Component* c = (rit != conn.remap.end()) ? reg.find(rit->second) : nullptr;
        if ((c == nullptr) || !e.is_alive()) {
            continue;
        }
        serialize::Reader cr(cb.bytes.data(), cb.bytes.size());
        if (c->name == "Position") {
            Position t{};
            NetworkRegistry::decode(*c, &t, cr);
            pos = t.value;
            got_pos = true;
        } else if (c->name == "Rotation") {
            Rotation t{};
            NetworkRegistry::decode(*c, &t, cr);
            rot = t.angle;
            got_rot = true;
        } else {
            if (c->name == "Bullet") {
                is_bullet = true;
            }
            NetworkRegistry::read(world, e, *c, cr);
        }
    }

    glm::vec2 bvel{0};
    if (is_bullet && e.is_alive() && e.has<Bullet>()) {
        bvel = math::heading(rot) * e.get<Bullet>().speed;
    }

    bool teleported = had_spawn && e.is_alive() && e.has<Spawn>() && e.get<Spawn>().epoch != old_epoch;
    if (teleported) {
        e.add<Teleported>();
        if (e.has<Dying>()) {
            e.remove<Dying>();
        }
    }

    if (e.is_alive() && (got_pos || got_rot)) {
        bool has_interp = e.has<Interpolation>();
        bool ready = has_interp && e.get<Interpolation>().ready;
        if (got_pos || ready) {
            glm::vec2 p = got_pos || !has_interp ? pos : e.get<Interpolation>().position;
            float a = got_rot || !has_interp ? rot : e.get<Interpolation>().angle;
            if (!e.has<Position>() && (got_pos || has_interp)) {
                e.set<Position>({p});
            }
            if (!e.has<Rotation>() && (got_rot || has_interp)) {
                e.set<Rotation>({a});
            }
            if (has_interp) {
                auto& in = e.get_mut<Interpolation>();
                if (teleported) {
                    in.count = 0;
                    in.head = 0;
                }
                in.push(p, a, static_cast<double>(tick));
            }
        }
    }
    if (is_bullet && e.is_alive()) {
        e.set<VelocityLinear>({bvel});
    }
}

static void apply_welcome(flecs::world& world, NetworkConnection& conn, serialize::Reader& r) {
    auto msg = serialize::decode<MessageWelcome>(r);
    if (!r.valid()) {
        return;
    }
    if (msg.protocol != NETWORK_PROTOCOL) {
        SDL_Log("network: protocol mismatch (server %u, client %u)", msg.protocol, NETWORK_PROTOCOL);
        world.set<ConnectionStatus>({.state = ConnectionState::Disconnected, .reason = "Incompatible server version"});
        if (world.has<InterfacePage>()) {
            world.set(InterfacePage::Status);
        }
        world.entity().add<RequestQuit>();
        return;
    }
    if (msg.tickrate != 0 && msg.tickrate != static_cast<uint16_t>(std::lround(1.0 / TICK_DT))) {
        SDL_Log("network: server tickrate %u != client %d — timing will desync (only %d Hz supported)", msg.tickrate, static_cast<int>(std::lround(1.0 / TICK_DT)), static_cast<int>(std::lround(1.0 / TICK_DT)));
    }
    conn.peer_id = msg.peer_id;
    conn.self = msg.controlled_entity;
    conn.registry_version = msg.registry_version;
    uint64_t tick = msg.tick;

    auto& registry = world.get_mut<NetworkRegistry>();
    registry.version = msg.registry_version;
    registry.adopt(world, msg.components, conn.remap);

    conn.welcomed = true;
    conn.newest = tick;
    conn.newest_time = util::now();
    conn.playback = static_cast<double>(tick);
    conn.simulated = tick;
    conn.client_tick = tick + static_cast<uint64_t>(BUFFER_TARGET);

    world.set<ConnectionStatus>({.state = ConnectionState::Connected, .reason = ""});
    if (world.has<InterfacePage>()) {
        world.set(InterfacePage::Ingame);
    }

    serialize::Writer w = wire::message(Message::Ack);
    MessageAcknowledge ack{tick};
    serialize::encode(w, ack);
    wire::send(conn.server, w, CHANNEL_RELIABLE, true);

    SDL_Log("network: welcome peer=%u self=%llu components=%zu", conn.peer_id, static_cast<unsigned long long>(conn.self), world.get<NetworkRegistry>().components.size());
}

static auto peek_is_bullet(const NetworkRegistry& reg, NetworkConnection& conn, const std::vector<MessageComponentData>& comps) -> bool {
    for (const auto& cb : comps) {
        auto rit = conn.remap.find(cb.server_id);
        const NetworkRegistry::Component* c = (rit != conn.remap.end()) ? reg.find(rit->second) : nullptr;
        if ((c != nullptr) && c->name == "Bullet") {
            return true;
        }
    }
    return false;
}

static void apply_snapshot(flecs::world& world, NetworkConnection& conn, serialize::Reader& r, std::span<const uint8_t> raw) {
    const auto& reg = world.get<NetworkRegistry>();
    auto snap = serialize::decode<MessageSnapshot>(r);
    if (!r.valid()) {
        return;
    }
    if (snap.registry_version != conn.registry_version) {
        if (snap.registry_version > conn.registry_version && !raw.empty()) {
            conn.deferred_snapshot.assign(raw.begin(), raw.end());
        }
        return;
    }
    conn.deferred_snapshot.clear();
    uint64_t tick = snap.tick;
    uint64_t ack = snap.acknowledged_tick;
    uint32_t buffer = snap.input_buffer;
    double stamp = snap.send_time;
    double t = util::now();

    if (stamp > conn.last_stamp) {
        conn.last_stamp = stamp;
        double rt = t - stamp;
        double cap = conn.rtt > 0 ? (conn.rtt * 2.0) + 0.05 : rt;
        conn.rtt = conn.rtt > 0 ? (conn.rtt * 0.9) + (std::min(rt, cap) * 0.1) : rt;
    }

    if (conn.newest != 0 && tick > conn.newest) {
        double arrived = (t - conn.newest_time) / TICK_DT;
        auto expected = static_cast<double>(tick - conn.newest);
        conn.jitter = (conn.jitter * 0.9) + (std::abs(arrived - expected) * 0.1);
    }
    if (conn.newest != 0 && tick < conn.newest) {
        return;
    }
    if (tick >= conn.newest) {
        conn.newest = tick;
        conn.newest_time = t;
    }
    conn.delay = std::clamp(2.0 + (conn.jitter * 2.0) + 1.0, 3.0, 12.0);

    conn.simulated = std::max(conn.simulated, ack);
    conn.buffer = buffer;
    auto& cmds = conn.commands;
    std::erase_if(cmds, [&](const Command& c) -> bool { return c.tick <= ack; });

    for (const auto& d : snap.deltas) {
        auto it = conn.entities.find(d.network_id);
        if (it != conn.entities.end() && world.entity(it->second).is_alive()) {
            apply_components(world, reg, conn, world.entity(it->second), d.components, tick);
        } else if (it != conn.entities.end()) {
            conn.entities.erase(it);
        }
    }

    serialize::Writer w = wire::message(Message::Ack);
    MessageAcknowledge ack_msg{tick};
    serialize::encode(w, ack_msg);
    wire::send(conn.server, w, CHANNEL_UNRELIABLE, false);
}

static void apply_structural(flecs::world& world, NetworkConnection& conn, serialize::Reader& r) {
    const auto& reg = world.get<NetworkRegistry>();
    auto s = serialize::decode<MessageStructural>(r);
    if (!r.valid()) {
        return;
    }
    uint64_t tick = s.tick;

    for (const auto& sp : s.spawns) {
        uint64_t nid = sp.network_id;
        bool fresh = !conn.entities.contains(nid);
        bool newBullet = fresh && peek_is_bullet(reg, conn, sp.components);
        flecs::entity e = mirror(world, conn, nid);
        apply_components(world, reg, conn, e, sp.components, tick);
        if (newBullet && e.is_alive()) {
            e.add<Latent>();
        }
        auto& dq = conn.despawn_queue;
        std::erase_if(dq, [&](const auto& d) -> auto { return d.first == nid; });
    }

    for (uint64_t nid : s.despawns) {
        auto eit = conn.entities.find(nid);
        if (eit != conn.entities.end()) {
            conn.despawn_queue.emplace_back(nid, tick);
            flecs::entity e = world.entity(eit->second);
            if (e.is_alive() && e.has<Dying>()) {
                e.set<Dying>({UINT64_MAX});
            }
        }
    }
}

static void apply_registry(flecs::world& world, NetworkConnection& conn, serialize::Reader& r) {
    auto msg = serialize::decode<MessageRegistry>(r);
    if (!r.valid()) {
        return;
    }
    auto& registry = world.get_mut<NetworkRegistry>();
    registry.version = msg.registry_version;
    registry.adopt(world, msg.components, conn.remap);
    conn.registry_version = msg.registry_version;
    SDL_Log("network: adopted registry update v%u (%zu components)", msg.registry_version, msg.components.size());

    if (!conn.deferred_snapshot.empty()) {
        std::vector<uint8_t> buf = std::move(conn.deferred_snapshot);
        conn.deferred_snapshot.clear();
        serialize::Reader rr(buf.data(), buf.size());
        (void)rr.get<uint8_t>();
        apply_snapshot(world, conn, rr, buf);
    }
}

static void apply_manifest(flecs::world& world, serialize::Reader& r) {
    auto msg = serialize::decode<MessageManifest>(r);
    if (!r.valid()) {
        return;
    }
    RequestAssetAdopt req;
    req.version = msg.version;
    req.entries.reserve(msg.entries.size());
    for (const auto& e : msg.entries) {
        req.entries.push_back({.name = e.name, .hash = e.hash, .kind = static_cast<AssetKind>(e.kind), .size = e.size});
    }
    world.entity().set(std::move(req));
}

void apply_packet(flecs::world& world, NetworkConnection& conn, ENetPacket* packet) {
    serialize::Reader r(packet->data, packet->dataLength);
    switch (static_cast<Message>(r.get<uint8_t>())) {
        case Message::Welcome:
            apply_welcome(world, conn, r);
            break;
        case Message::Snapshot:
            if (conn.welcomed) {
                apply_snapshot(world, conn, r, {packet->data, packet->dataLength});
            }
            break;
        case Message::Structural:
            if (conn.welcomed) {
                apply_structural(world, conn, r);
            }
            break;
        case Message::Registry:
            if (conn.welcomed) {
                apply_registry(world, conn, r);
            }
            break;
        case Message::Manifest:
            if (conn.welcomed) {
                apply_manifest(world, r);
            }
            break;
        case Message::AssetChunk:
            if (conn.welcomed) {
                auto msg = serialize::decode<MessageAssetChunk>(r);
                if (r.valid()) {
                    world.entity().set(RequestAssetStore{.hash = msg.hash, .offset = msg.offset, .total = msg.total, .bytes = std::move(msg.bytes)});
                }
            }
            break;
        case Message::Tileset:
            if (conn.welcomed) {
                auto msg = serialize::decode<MessageTileset>(r);
                if (r.valid()) {
                    std::vector<TileType> types;
                    types.reserve(msg.types.size());
                    for (const MessageTileType& t : msg.types) {
                        types.push_back({.texture = t.texture, .solid = t.solid != 0, .restitution = t.restitution, .friction = t.friction, .drag = t.drag, .hp = t.hp});
                    }
                    world.entity().set(RequestLoadTileset{.types = std::move(types)});
                }
            }
            break;
        case Message::TileChunk:
            if (conn.welcomed) {
                auto msg = serialize::decode<MessageTileChunk>(r);
                if (r.valid()) {
                    world.entity().set(RequestLoadChunk{.cx = msg.cx, .cy = msg.cy, .tiles = std::move(msg.tiles)});
                }
            }
            break;
        case Message::TileSet:
            if (conn.welcomed) {
                auto msg = serialize::decode<MessageTileSet>(r);
                if (r.valid()) {
                    world.entity().set(RequestSetTile{.tx = msg.tx, .ty = msg.ty, .id = msg.id});
                }
            }
            break;
        case Message::TileUnload:
            if (conn.welcomed) {
                auto msg = serialize::decode<MessageTileUnload>(r);
                if (r.valid()) {
                    world.entity().set(RequestUnloadChunk{.cx = msg.cx, .cy = msg.cy});
                }
            }
            break;
        case Message::Sound: {
            auto msg = serialize::decode<MessageSound>(r);
            if (r.valid()) {
                world.entity().set<RequestSound>({.asset = msg.asset, .x = msg.x, .y = msg.y, .volume = msg.volume, .global = msg.global});
            }
            break;
        }
        case Message::Particles: {
            auto msg = serialize::decode<MessageParticles>(r);
            if (r.valid()) {
                world.entity().set<RequestParticles>({
                    .position = {msg.x, msg.y}, .count = msg.count, .texture = msg.texture,
                    .direction = msg.direction, .spread = msg.spread,
                    .speed = {msg.speed_min, msg.speed_max}, .size = {msg.size_min, msg.size_max}, .life = {msg.life_min, msg.life_max},
                    .gravity = msg.gravity, .drag = msg.drag, .spin = msg.spin, .grow = msg.grow,
                    .color_begin = {msg.cb_r, msg.cb_g, msg.cb_b, msg.cb_a}, .color_end = {msg.ce_r, msg.ce_g, msg.ce_b, msg.ce_a},
                    .emissive = msg.emissive, .bounce = msg.bounce, .collide = msg.collide != 0,
                    .blend = static_cast<BlendMode>(msg.blend),
                });
            }
            break;
        }
        case Message::Transition: {
            auto msg = serialize::decode<MessageTransition>(r);
            if (r.valid()) {
                world.entity().set<RequestTransition>({
                    .kind = static_cast<ScreenTransitionKind>(msg.kind), .duration = msg.duration,
                    .color = {msg.r, msg.g, msg.b, msg.a}, .center = {msg.center_x, msg.center_y}, .direction = msg.direction,
                });
            }
            break;
        }
        case Message::Chat: {
            auto msg = serialize::decode<MessageChat>(r);
            if (r.valid()) {
                if (ChatLog* log = world.try_get_mut<ChatLog>()) {
                    log->push(msg.text);
                }
            }
            break;
        }
        case Message::CommandList: {
            auto msg = serialize::decode<MessageCommandList>(r);
            if (!r.valid()) {
                break;
            }
            CommandBook book;
            for (auto& entry : msg.commands) {
                book.commands.push_back(from_message_command(entry));
            }
            world.set<CommandBook>(std::move(book));
            break;
        }
        case Message::ViewOpen: {
            auto msg = serialize::decode<MessageViewOpen>(r);
            if (!r.valid()) {
                break;
            }
            if (auto* view = world.try_get_mut<ViewState>()) {
                auto placement = static_cast<ViewPlacement>(msg.placement);
                std::erase_if(view->views, [&](const ViewActive& s) -> bool { return s.id == msg.id; });
                view->views.push_back({.id = msg.id, .placement = placement, .root = to_view_widget(msg.root)});
                if (placement == ViewPlacement::Center) {
                    if (auto* page = world.try_get<InterfacePage>(); page != nullptr && *page == InterfacePage::Chat) {
                        world.set(InterfacePage::Ingame);
                    }
                }
            }
            break;
        }
        case Message::ViewClose: {
            auto msg = serialize::decode<MessageViewClose>(r);
            if (!r.valid()) {
                break;
            }
            if (auto* view = world.try_get_mut<ViewState>()) {
                std::erase_if(view->views, [&](const ViewActive& s) -> bool { return s.id == msg.id; });
            }
            break;
        }
        case Message::Kick: {
            auto msg = serialize::decode<MessageKick>(r);
            if (!r.valid()) {
                break;
            }
            world.set<ConnectionStatus>({.state = ConnectionState::Disconnected, .reason = msg.reason});
            if (world.has<InterfacePage>()) {
                world.set(InterfacePage::Status);
            }
            break;
        }
        default:
            break;
    }
}
