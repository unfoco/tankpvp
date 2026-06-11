#include "network.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <utility>

#include "component/asset.h"
#include "component/audio.h"
#include "component/effect.h"
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
    world.component<MovementStats>().member<float>("speed").member<float>("turn");
    world.component<WeaponStats>().member<uint32_t>("cooldown").member<float>("speed").member<float>("muzzle").member<float>("life");
    world.component<Bullet>().member<float>("speed");
    world.component<Spawn>().member<uint16_t>("epoch");
    world.component<Dying>().member<uint64_t>("revive");
    world.component<VisionKind>();
    world.component<Camera>()
        .member<uint64_t>("target").member<float>("focus_x").member<float>("focus_y").member<float>("offset_x").member<float>("offset_y")
        .member<float>("zoom").member<float>("rotation").member<float>("follow").member<float>("shake").member<float>("ambient")
        .member<VisionKind>("vision").member<float>("vision_range").member<float>("vision_angle").member<float>("shadow_solid")
        .member<float>("tint_r").member<float>("tint_g").member<float>("tint_b").member<float>("tint_a")
        .member<float>("flash").member<float>("flash_fade").member<float>("vignette").member<float>("blur").member<float>("chromatic");
    world.component<Light>().member<float>("r").member<float>("g").member<float>("b").member<float>("radius").member<float>("intensity");
    world.component<Environment>().member<float>("bg_r").member<float>("bg_g").member<float>("bg_b");
    world.component<Loading>().member<float>("active");
    world.component<VisionBlocker>().member<float>("radius").member<float>("alpha").member<float>("r").member<float>("g").member<float>("b");
    world.component<Ammo>().member<uint32_t>("mag").member<uint32_t>("reserve").member<uint32_t>("mag_size").member<float>("reload_time").member<float>("reloading");
    world.component<BlendMode>();
    world.component<RenderLayer>();
    world.component<Blend>().member<float>("opacity").member<BlendMode>("mode");
    world.component<Layer>().member<RenderLayer>("value");
    world.component<Sprite>()
        .member<uint64_t>("texture", SPRITE_LAYERS)
        .member<float>("offset_x", SPRITE_LAYERS)
        .member<float>("offset_y", SPRITE_LAYERS)
        .member<float>("pivot_x", SPRITE_LAYERS)
        .member<float>("pivot_y", SPRITE_LAYERS);

    world.component<Sprite>().add<Networked>();
    world.component<Position>().add<Networked>().set<Quantize>({.precision = 1.0F / 8192.0F, .bytes = 4});
    world.component<Rotation>().add<Networked>().set<Quantize>({.precision = 0.0001F, .bytes = 2});
    world.component<Color>().add<Networked>();
    world.component<Owner>().add<Networked>();
    world.component<Tank>().add<Networked>();
    world.component<Decoration>().add<Networked>();
    world.component<Dying>().add<Networked>();
    world.component<Camera>().add<Networked>();
    world.component<Light>().add<Networked>();
    world.component<Environment>().add<Networked>();
    world.component<Loading>().add<Networked>();
    world.component<VisionBlocker>().add<Networked>();
    world.component<Ammo>().add<Networked>();
    world.component<Blend>().add<Networked>();
    world.component<Layer>().add<Networked>();
    world.component<Bullet>().add<Networked>();
    world.component<Spawn>().add<Networked>();
    world.component<MovementStats>().add<Networked>();
    world.component<WeaponStats>().add<Networked>();
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

    world.observer("network::tank_sprite").with<Tank>().without<Sprite>().event(flecs::OnAdd).each([](flecs::entity e) -> void { e.set<Sprite>(tank_default_sprite()); });
    world.observer("network::tank_spawn").with<Tank>().without<Spawn>().event(flecs::OnAdd).each([](flecs::entity e) -> void { e.set<Spawn>({}); });

    world.observer<const Position>("network::decoration_setup").with<Decoration>().without<Frozen>().event(flecs::OnSet).each([](flecs::entity e, const Position& pos) -> void {
        flecs::world w = e.world();
        if (!w.has<NetworkHost>()) {
            return;
        }
        e.add<Frozen>();
        if (!e.has<Layer>()) {
            const auto* grid = w.try_get<WorldGrid>();
            const auto* tileset = w.try_get<Tileset>();
            bool over = grid != nullptr && tileset != nullptr && ballistics::solid(ballistics::tile_at(*grid, *tileset, pos.value.x, pos.value.y));
            e.set<Layer>({over ? RenderLayer::Overlay : RenderLayer::Ground});
        }
    });

    world.system<const RequestSound>("network::sounds").kind(flecs::OnStore).each([](flecs::entity e, const RequestSound&) -> void { e.destruct(); });
    world.system<const RequestParticles>("network::particles_gc").kind(flecs::OnStore).each([](flecs::entity e, const RequestParticles&) -> void { e.destruct(); });

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

    if (std::getenv("TANKPVP_BOTS") != nullptr) {
        for (int i = 0; i < 50; i++) {
            world.entity()
                .set(Color{.value = {static_cast<float>(rand() % 255), static_cast<float>(rand() % 255), static_cast<float>(rand() % 255)}})
                .set(Position{.value = {200.0F + static_cast<float>(rand() % 2000), 200.0F + static_cast<float>(rand() % 2000)}})
                .set(Rotation{.angle = 0})
                .set(VelocityLinear{})
                .set(VelocityAngular{})
                .set(CollisionBox{.height = 30, .width = 40})
                .set(DampingLinear{.value = 8.0F})
                .set(DampingAngular{.value = 1.0F})
                .set<InputFlags>(i % 5 == 0 ? InputFlags::Left | InputFlags::Forward : InputFlags::None)
                .add<Dynamic>()
                .add<Tank>()
                .add<History>()
                .add<Replicated>();
        }
    }

    if (!dedicated) {
        flecs::entity tank = world.entity()
            .set(Color{.value = {255.0F, 50.0F, 50.0F}})
            .set(Position{.value = {640.0F, 400.0F}})
            .set(Rotation{.angle = 0})
            .set(VelocityLinear{})
            .set(VelocityAngular{})
            .set(CollisionBox{.height = 30, .width = 40})
            .set(Owner{.peer = 0})
            .set(Firing{})
            .add<InputFlags>()
            .add<Dynamic>()
            .add<Tank>()
            .add<Local>()
            .add<History>()
            .add<ViewLag>()
            .add<Replicated>();

        flecs::entity host_peer = world.entity().set<Peer>({.peer = nullptr, .id = 0, .welcomed = true}).add<Controls>(tank);

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
