#include "network.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <utility>

#include "component/object.h"
#include "component/physics.h"
#include "component/input.h"

#include "registry.h"
#include "client.h"
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

    world.component<Position>().add<Networked>().set<Quantize>({1.0f / 8192.0f, 4});
    world.component<Rotation>().add<Networked>().set<Quantize>({0.0001f, 2});
    world.component<Color>().add<Networked>();
    world.component<Owner>().add<Networked>();
    world.component<Tank>().add<Networked>();
    world.component<Bullet>().add<Networked>();
    world.component<CollisionBox>().add<Networked>();
    world.component<DampingLinear>().add<Networked>();
    world.component<DampingAngular>().add<Networked>();

    NetworkRegistry registry;
    registry.build(world);
    SDL_Log("network: registry built with %zu replicated components", registry.components.size());
    world.set<NetworkRegistry>(std::move(registry));

    world.set<NetworkTarget>({});

    world.observer<const NetworkRequestHost>("network::host")
        .event(flecs::OnSet).each(Network::host);
    world.observer<const NetworkRequestJoin>("network::join")
        .event(flecs::OnSet).each(Network::join);
    world.observer()
        .with<NetworkRequestQuit>().event(flecs::OnAdd)
        .each([](flecs::entity e) { Network::quit(e, NetworkRequestQuit{}); });

    world.import<NetworkServer>();
    world.import<NetworkClient>();
}

void Network::host(flecs::entity e, const NetworkRequestHost& req) {
    flecs::world world = e.world();
    if (world.try_get<NetworkHost>() || world.try_get<NetworkConnection>()) { e.destruct(); return; }

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = req.port;
    ENetHost* host = enet_host_create(&address, 32, CHANNEL_COUNT, 0, 0);
    if (!host) {
        SDL_Log("network: failed to create server host on port %u", req.port);
        e.destruct();
        return;
    }

    const NetworkConfig* cfg = world.try_get<NetworkConfig>();
    bool dedicated = cfg && cfg->role == NetworkRole::Server;
    world.set<NetworkHost>({.host = host, .tickrate = static_cast<uint16_t>(cfg ? cfg->tickrate : 60)});
    SDL_Log("network: hosting on port %u", req.port);

    for (int i = 0; i < 500; i++) {
        world.entity()
            .set(Color{.value = {(float)(rand() % 255), (float)(rand() % 255), (float)(rand() % 255)}})
            .set(Position{.value = {200.0f + rand() % 2000, 200.0f + rand() % 2000}})
            .set(Rotation{.angle = 0})
            .set(VelocityLinear{})
            .set(VelocityAngular{})
            .set(CollisionBox{.height = 30, .width = 40})
            .set(DampingLinear{.value = 8.0f})
            .set(DampingAngular{.value = 1.0f})
            .set<InputFlags>(i % 5 == 0 ? InputFlags::Left | InputFlags::Forward : InputFlags::None)
            .add<Dynamic>()
            .add<Tank>()
            .add<History>()
            .add<Replicated>();
    }

    if (!dedicated) {
        world.entity()
            .set(Color{.value = {255.0f, 50.0f, 50.0f}})
            .set(Position{.value = {640.0f, 400.0f}})
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
    }

    e.destruct();
}

void Network::join(flecs::entity e, const NetworkRequestJoin& req) {
    flecs::world world = e.world();
    if (world.try_get<NetworkConnection>() || world.try_get<NetworkHost>()) { e.destruct(); return; }

    ENetHost* client = enet_host_create(nullptr, 1, CHANNEL_COUNT, 0, 0);
    if (!client) {
        SDL_Log("network: failed to create client host");
        e.destruct();
        return;
    }

    ENetAddress address{};
    enet_address_set_host(&address, req.address.c_str());
    address.port = req.port;
    ENetPeer* server = enet_host_connect(client, &address, CHANNEL_COUNT, 0);
    if (!server) {
        SDL_Log("network: no available peers for connection");
        enet_host_destroy(client);
        e.destruct();
        return;
    }

    world.set<NetworkConnection>({.host = client, .server = server});
    SDL_Log("network: connecting to %s:%u", req.address.c_str(), req.port);

    e.destruct();
}

void Network::quit(flecs::entity e, const NetworkRequestQuit&) {
    flecs::world world = e.world();

    if (const NetworkHost* h = world.try_get<NetworkHost>(); h && h->host) {
        ENetHost* host = h->host;
        NetworkServer::teardown(world);
        enet_host_destroy(host);
        world.remove<NetworkHost>();
        SDL_Log("network: stopped hosting");
    }

    if (const NetworkConnection* c = world.try_get<NetworkConnection>(); c && c->host) {
        ENetHost* client = c->host;
        ENetPeer* server = c->server;
        NetworkClient::teardown(world);
        if (server) enet_peer_disconnect_now(server, 0);
        enet_host_destroy(client);
        world.remove<NetworkConnection>();
        SDL_Log("network: disconnected");
    }

    e.destruct();
}
