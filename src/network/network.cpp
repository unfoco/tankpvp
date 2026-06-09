#include "network.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <utility>

#include "component/asset.h"
#include "component/audio.h"
#include "component/network.h"
#include "component/interface.h"
#include "component/input.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/script.h"
#include "component/settings.h"

#include "client.h"
#include "protocol.h"
#include "registry.h"
#include "server.h"

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
    world.component<Bullet>().add<Networked>();
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

    world.system<const RequestSound>("network::sounds").kind(flecs::OnStore).each([](flecs::entity e, const RequestSound&) -> void { e.destruct(); });

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
    world.set<NetworkHost>({.host = host, .tickrate = static_cast<uint16_t>((cfg != nullptr) ? cfg->tickrate : 60)});
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
