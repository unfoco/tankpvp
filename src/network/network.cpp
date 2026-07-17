#include "network.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <utility>

#include "component/audio.h"
#include "component/network.h"
#include "component/interface.h"
#include "component/input.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/render.h"
#include "component/script.h"
#include "util/ballistics.h"
#include "component/settings.h"

#include "client.h"
#include "protocol.h"
#include "registry.h"
#include "server.h"

static uint16_t g_query_tickrate = 60;

static int ENET_CALLBACK query_intercept(ENetHost* host, ENetEvent*) {
    if (host->receivedDataLength < sizeof(uint32_t)) {
        return 0;
    }
    serialize::Reader r(host->receivedData, host->receivedDataLength);
    if (r.get<uint32_t>() != QUERY_MAGIC) {
        return 0;
    }
    auto ping = serialize::decode<MessagePing>(r);
    if (!r.valid()) {
        return 1;
    }
    serialize::Writer w;
    w.put<uint32_t>(QUERY_MAGIC);
    MessagePong pong{
        .protocol = NETWORK_PROTOCOL,
        .token = ping.token,
        .players = static_cast<uint16_t>(host->connectedPeers),
        .max_players = MAX_PLAYERS,
        .tickrate = g_query_tickrate,
    };
    serialize::encode(w, pong);
    ENetBuffer out{.data = w.data.data(), .dataLength = w.data.size()};
    enet_socket_send(host->socket, &host->receivedAddress, &out, 1);
    return 1;
}

Network::Network(flecs::world& world) {
    if (enet_initialize() != 0) {
        SDL_Log("network: failed to initialize ENet");
        exit(EXIT_FAILURE);
    }
    atexit(enet_deinitialize);

    world.component<Position>().member<float>("x").member<float>("y");
    world.component<Rotation>().member<float>("angle");
    world.component<Color>().member<float>("r").member<float>("g").member<float>("b");
    world.component<Owner>().member<uint32_t>("peer").member<uint32_t>("prediction");
    world.component<DifferentialStats>().member<float>("speed").member<float>("turn");
    world.component<TopDownStats>().member<float>("speed").member<float>("accel").member<float>("face_rate");
    world.component<PlatformerStats>().member<float>("speed").member<float>("accel").member<float>("air_control").member<float>("jump");
    world.component<GravityScale>().member<float>("value");
    world.component<Gravity>().member<float>("x").member<float>("y");
    world.component<Soundtrack>().member<uint64_t>("hash");
    world.component<Controller>().member<ControlScheme>("scheme");
    world.component<ProjectileWeapon>().member<uint32_t>("cooldown").member<float>("speed").member<float>("muzzle").member<float>("life").member<uint64_t>("sound");
    world.component<Projectile>().member<float>("speed").member<float>("gravity_scale").member<uint8_t>("bounces").member<uint8_t>("pierce").member<uint64_t>("sound");
    world.component<ProjectileSprite>().member<uint64_t>("texture");
    world.component<Spawn>().member<uint16_t>("epoch");
    world.component<Dying>().member<uint64_t>("revive");
    world.component<VisionKind>();
    world.component<ControlScheme>();
    world.component<BlendMode>();
    world.component<Camera>()
        .member<uint64_t>("target").member<float>("focus_x").member<float>("focus_y").member<float>("offset_x").member<float>("offset_y")
        .member<float>("zoom").member<float>("rotation").member<float>("follow").member<float>("shake");
    world.component<Vision>()
        .member<VisionKind>("kind").member<float>("range").member<float>("angle").member<bool>("solid")
        .member<float>("ambient").member<float>("ambient_r").member<float>("ambient_g").member<float>("ambient_b");
    world.component<PostStack>()
        .member<float>("tint_r").member<float>("tint_g").member<float>("tint_b").member<float>("tint_a")
        .member<float>("flash").member<float>("flash_fade").member<float>("vignette").member<float>("blur").member<float>("chromatic")
        .member<float>("pixelate").member<float>("crt").member<float>("dither").member<float>("saturation").member<float>("distortion");
    world.component<Light>()
        .member<float>("r").member<float>("g").member<float>("b").member<float>("radius").member<float>("intensity")
        .member<float>("cone").member<float>("softness").member<bool>("shadows").member<float>("flicker");
    world.component<Occluder>().member<float>("half_x").member<float>("half_y").member<float>("opacity");
    world.component<Environment>()
        .member<float>("bg_r").member<float>("bg_g").member<float>("bg_b")
        .member<uint64_t>("texture").member<float>("texture_size")
        .member<float>("mod_r").member<float>("mod_g").member<float>("mod_b");
    world.component<Loading>().member<float>("active");
    world.component<VisionBlocker>().member<float>("radius").member<float>("strength").member<float>("r").member<float>("g").member<float>("b");
    world.component<RadarVisible>().member<float>("r").member<float>("g").member<float>("b").member<float>("radius").member<bool>("through_walls");
    world.component<Ammo>().member<uint32_t>("mag").member<uint32_t>("reserve").member<uint32_t>("mag_size").member<float>("reload_time").member<float>("reloading");
    world.component<Blend>().member<float>("opacity").member<BlendMode>("mode");
    world.component<RenderDepth>().member<int16_t>("plane").member<bool>("y_sort");
    world.component<Attach>()
        .member<uint64_t>("parent").member<float>("offset_x").member<float>("offset_y")
        .member<float>("rotation").member<bool>("inherit_rotation");
    world.component<SpritePart>().member<uint8_t>("index");
    world.component<Sprite>()
        .member<uint64_t>("texture")
        .member<float>("size_x").member<float>("size_y")
        .member<float>("pivot_x").member<float>("pivot_y")
        .member<float>("offset_x").member<float>("offset_y")
        .member<float>("region", 4)
        .member<bool>("flip_x").member<bool>("flip_y");
    world.component<Material>()
        .member<uint64_t>("shader").member<uint64_t>("normal_map")
        .member<float>("emissive").member<float>("dissolve")
        .member<float>("edge_r").member<float>("edge_g").member<float>("edge_b").member<float>("edge_a")
        .member<float>("distortion").member<float>("params", 8);
    world.component<ParticleEmitter>()
        .member<float>("rate").member<uint16_t>("burst").member<uint64_t>("texture")
        .member<float>("spawn_x").member<float>("spawn_y")
        .member<float>("direction").member<float>("spread")
        .member<float>("speed_min").member<float>("speed_max")
        .member<float>("size_min").member<float>("size_max")
        .member<float>("life_min").member<float>("life_max")
        .member<float>("gravity").member<float>("drag").member<float>("spin").member<float>("grow")
        .member<float>("begin_r").member<float>("begin_g").member<float>("begin_b").member<float>("begin_a")
        .member<float>("end_r").member<float>("end_g").member<float>("end_b").member<float>("end_a")
        .member<float>("emissive").member<float>("bounce")
        .member<bool>("collide").member<bool>("local_space").member<BlendMode>("blend");

    world.component<Sprite>().add<Networked>();
    world.component<Position>().add<Networked>().set<Quantize>({.precision = 1.0F / 8192.0F, .bytes = 4});
    world.component<Rotation>().add<Networked>().set<Quantize>({.precision = 0.0001F, .bytes = 2});
    world.component<Color>().add<Networked>();
    world.component<Owner>().add<Networked>();
    world.component<Decoration>().add<Networked>();
    world.component<Hidden>().add<Networked>();
    world.component<Dying>().add<Networked>();
    world.component<Camera>().add<Networked>();
    world.component<Vision>().add<Networked>();
    world.component<PostStack>().add<Networked>();
    world.component<Light>().add<Networked>();
    world.component<Occluder>().add<Networked>();
    world.component<Environment>().add<Networked>();
    world.component<Loading>().add<Networked>();
    world.component<VisionBlocker>().add<Networked>();
    world.component<RadarVisible>().add<Networked>();
    world.component<Ammo>().add<Networked>();
    world.component<Blend>().add<Networked>();
    world.component<RenderDepth>().add<Networked>();
    world.component<Attach>().add<Networked>();
    world.component<Material>().add<Networked>();
    world.component<ParticleEmitter>().add<Networked>();
    world.component<Projectile>().add<Networked>();
    world.component<ProjectileSprite>().add<Networked>();
    world.component<Spawn>().add<Networked>();
    world.component<DifferentialStats>().add<Networked>();
    world.component<TopDownStats>().add<Networked>();
    world.component<PlatformerStats>().add<Networked>();
    world.component<GravityScale>().add<Networked>();
    world.component<Gravity>().add<Networked>();
    world.component<Soundtrack>().add<Networked>();
    world.component<VelocityLinear>().add<Networked>().set<Quantize>({.precision = 1.0F / 16.0F, .bytes = 2});
    world.component<Controller>().add<Networked>();
    world.component<ProjectileWeapon>().add<Networked>();
    world.component<CollisionBox>().add<Networked>();
    world.component<DampingLinear>().add<Networked>();
    world.component<DampingAngular>().add<Networked>();

    NetworkRegistry registry;
    registry.build(world);
    SDL_Log("network: registry built with %zu replicated components", registry.components.size());
    world.set<NetworkRegistry>(std::move(registry));

    world.set<NetworkTarget>({});
    world.set<ConnectionStatus>({});

    world.observer<const RequestHost>("network::host").event(flecs::OnSet).each(Network::host);
    world.observer<const RequestJoin>("network::join").event(flecs::OnSet).each(Network::join);
    world.observer().with<RequestQuit>().event(flecs::OnAdd).each([](flecs::entity e) -> void { Network::quit(e, RequestQuit{}); });

    world.observer("network::hitbox_setup").with<HitBox>().event(flecs::OnAdd).each([](flecs::entity e) -> void {
        if (!e.has<History>()) {
            e.add<History>();
        }
        if (!e.has<ViewLag>()) {
            e.add<ViewLag>();
        }
        if (!e.has<Spawn>()) {
            e.set<Spawn>({});
        }
    });

    world.observer("network::controllable_setup").with<Controller>().event(flecs::OnAdd).each([](flecs::entity e) -> void {
        if (!e.has<InputState>()) {
            e.set<InputState>({});
        }
        if (!e.has<VelocityLinear>()) {
            e.set<VelocityLinear>({});
        }
        if (!e.has<VelocityAngular>()) {
            e.set<VelocityAngular>({});
        }
    });
    world.observer("network::weapon_setup").with<ProjectileWeapon>().event(flecs::OnAdd).each([](flecs::entity e) -> void {
        if (!e.has<Firing>()) {
            e.set<Firing>({});
        }
    });

    world.observer<const Position>("network::decoration_setup").with<Decoration>().without<Frozen>().event(flecs::OnSet).each([](flecs::entity e, const Position& pos) -> void {
        flecs::world w = e.world();
        if (!w.has<NetworkHost>()) {
            return;
        }
        e.add<Frozen>();
        if (!e.has<RenderDepth>()) {
            const auto* grid = w.try_get<WorldGrid>();
            const auto* tileset = w.try_get<Tileset>();
            bool over = grid != nullptr && tileset != nullptr && ballistics::solid(ballistics::tile_at(*grid, *tileset, pos.value.x, pos.value.y));
            e.set<RenderDepth>({.plane = over ? plane::Overhead + 100 : plane::Entity - 100, .y_sort = false});
        }
    });

    world.system<const RequestSound>("network::sounds").kind(flecs::OnStore).each([](flecs::entity e, const RequestSound&) -> void { e.destruct(); });
    world.system<const RequestParticles>("network::particles_gc").kind(flecs::OnStore).each([](flecs::entity e, const RequestParticles&) -> void { e.destruct(); });

    world.system<Attach>("network::attach_link").kind(flecs::OnStore).each([](flecs::entity e, Attach& attach) -> void {
        if (attach.parent != 0 || !e.world().has<NetworkHost>()) {
            return;
        }
        flecs::entity parent = e.parent();
        if (parent && parent.is_alive()) {
            if (const auto* nid = parent.try_get<NetworkId>()) {
                attach.parent = nid->value;
                e.modified<Attach>();
            }
        }
    });

    world.import<NetworkServer>();
    world.import<NetworkClient>();
}

static void rebuild_registry(flecs::world world) {
    NetworkRegistry rebuilt;
    rebuilt.build(world);
    const auto* previous = world.try_get<NetworkRegistry>();
    rebuilt.version = static_cast<uint16_t>((previous != nullptr ? previous->version : 0) + 1);
    SDL_Log("network: registry rebuilt (v%u) with %zu replicated components", rebuilt.version, rebuilt.components.size());
    world.set<NetworkRegistry>(std::move(rebuilt));
}

static void leave_session(flecs::world world) {
    if (const auto* h = world.try_get<NetworkHost>(); (h != nullptr) && (h->host != nullptr)) {
        ENetHost* host = h->host;
        for (size_t i = 0; i < host->peerCount; ++i) {
            if (host->peers[i].state == ENET_PEER_STATE_CONNECTED) {
                enet_peer_disconnect_now(&host->peers[i], 0);
            }
        }
        NetworkServer::teardown(world);
        enet_host_destroy(host);
        world.remove<NetworkHost>();
        world.get_mut<ServerClock>().running = false;
        SDL_Log("network: stopped hosting");
    }
    if (const auto* c = world.try_get<NetworkConnection>(); (c != nullptr) && (c->host != nullptr)) {
        ENetHost* client = c->host;
        ENetPeer* server = c->server;
        NetworkClient::teardown(world);
        if (server != nullptr) {
            enet_peer_disconnect_now(server, 0);
        }
        enet_host_destroy(client);
        world.remove<NetworkConnection>();
        SDL_Log("network: disconnected");
    }
}

void Network::host(flecs::entity e, const RequestHost& req) {
    flecs::world world = e.world();
    if (world.try_get<NetworkHost>() != nullptr) {
        e.destruct();
        return;
    }
    leave_session(world);

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = req.port;
    ENetHost* host = enet_host_create(&address, MAX_PLAYERS, CHANNEL_COUNT, 0, 0);
    if (host == nullptr) {
        SDL_Log("network: failed to create server host on port %u", req.port);
        e.destruct();
        return;
    }

    const auto* cfg = world.try_get<NetworkConfig>();
    bool dedicated = (cfg != nullptr) && cfg->role == NetworkRole::Server;
    auto tickrate = static_cast<uint16_t>((cfg != nullptr) ? cfg->tickrate : 60);
    g_query_tickrate = tickrate;
    host->intercept = query_intercept;
    world.set<NetworkHost>({.host = host, .tickrate = tickrate});
    SDL_Log("network: hosting on port %u", req.port);

    if (!dedicated) {

        flecs::entity host_peer = world.entity().set<Peer>({.peer = nullptr, .id = 0, .welcomed = true});

        std::string uname = world.has<Settings>() ? world.get<Settings>().username : std::string();
        world.entity().set(RequestPlayerJoin{.peer = host_peer, .username = uname.empty() ? "host" : uname});
    }

    e.destruct();
}

void Network::join(flecs::entity e, const RequestJoin& req) {
    flecs::world world = e.world();
    if (world.try_get<NetworkHost>() != nullptr) {
        e.destruct();
        return;
    }
    leave_session(world);
    rebuild_registry(world);

    ENetHost* client = enet_host_create(nullptr, 1, CHANNEL_COUNT, 0, 0);
    if (client == nullptr) {
        SDL_Log("network: failed to create client host");
        e.destruct();
        return;
    }

    ENetAddress address{};
    enet_address_set_host(&address, req.address.c_str());
    address.port = req.port;
    ENetPeer* server = enet_host_connect(client, &address, CHANNEL_COUNT, 0);
    if (server == nullptr) {
        SDL_Log("network: no available peers for connection");
        enet_host_destroy(client);
        e.destruct();
        return;
    }

    world.set<NetworkConnection>({.host = client, .server = server});
    SDL_Log("network: connecting to %s:%u", req.address.c_str(), req.port);

    e.destruct();
}

void Network::quit(flecs::entity e, const RequestQuit&) {
    flecs::world world = e.world();

    leave_session(world);

    if (auto* log = world.try_get_mut<ChatLog>()) {
        *log = {};
    }
    if (world.has<CommandBook>()) {
        world.remove<CommandBook>();
    }
    if (auto* view = world.try_get_mut<ViewState>()) {
        view->views.clear();
    }

    e.destruct();
}
