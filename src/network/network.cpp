#include "network.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <utility>

#include "client.h"
#include "component/interface.h"
#include "component/input.h"
#include "component/object.h"
#include "component/physics.h"
#include "component/settings.h"
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

    world.component<Position>().add<Networked>().set<Quantize>({.precision = 1.0F / 8192.0F, .bytes = 4});
    world.component<Rotation>().add<Networked>().set<Quantize>({.precision = 0.0001F, .bytes = 2});
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

    world.observer<const NetworkRequestHost>("network::host").event(flecs::OnSet).each(Network::host);
    world.observer<const NetworkRequestJoin>("network::join").event(flecs::OnSet).each(Network::join);
    world.observer().with<NetworkRequestQuit>().event(flecs::OnAdd).each([](flecs::entity e) -> void { Network::quit(e, NetworkRequestQuit{}); });
    world.observer<const NetworkRequestChat>("network::chat").event(flecs::OnSet).each(Network::chat);

    world.import<NetworkServer>();
    world.import<NetworkClient>();
}

void Network::host(flecs::entity e, const NetworkRequestHost& req) {
    flecs::world world = e.world();
    if ((world.try_get<NetworkHost>() != nullptr) || (world.try_get<NetworkConnection>() != nullptr)) {
        e.destruct();
        return;
    }

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = req.port;
    ENetHost* host = enet_host_create(&address, 32, CHANNEL_COUNT, 0, 0);
    if (host == nullptr) {
        SDL_Log("network: failed to create server host on port %u", req.port);
        e.destruct();
        return;
    }

    const auto* cfg = world.try_get<NetworkConfig>();
    bool dedicated = (cfg != nullptr) && cfg->role == NetworkRole::Server;
    world.set<NetworkHost>({.host = host, .tickrate = static_cast<uint16_t>((cfg != nullptr) ? cfg->tickrate : 60)});
    SDL_Log("network: hosting on port %u", req.port);

    for (int i = 0; i < 500; i++) {
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

    if (!dedicated) {
        world.entity()
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
    }

    e.destruct();
}

void Network::join(flecs::entity e, const NetworkRequestJoin& req) {
    flecs::world world = e.world();
    if ((world.try_get<NetworkConnection>() != nullptr) || (world.try_get<NetworkHost>() != nullptr)) {
        e.destruct();
        return;
    }

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

void Network::quit(flecs::entity e, const NetworkRequestQuit&) {
    flecs::world world = e.world();

    if (const auto* h = world.try_get<NetworkHost>(); (h != nullptr) && (h->host != nullptr)) {
        ENetHost* host = h->host;
        NetworkServer::teardown(world);
        enet_host_destroy(host);
        world.remove<NetworkHost>();
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

    if (auto* log = world.try_get_mut<ChatLog>()) {
        *log = {};
    }

    e.destruct();
}

void Network::chat(flecs::entity e, const NetworkRequestChat& req) {
    flecs::world world = e.world();
    std::string text = req.text;
    if (auto* conn = world.try_get_mut<NetworkConnection>(); (conn != nullptr) && conn->connected && (conn->server != nullptr)) {
        Writer w = wire::message(Message::Chat);
        MessageChat msg{text};
        util::encode(w, msg);
        wire::send(conn->server, w, CHANNEL_RELIABLE, true);
    } else if (world.try_get<NetworkHost>() != nullptr) {
        std::string name;
        if (const Settings* s = world.try_get<Settings>()) {
            name = s->username;
        }
        broadcast_chat(world, "<" + (name.empty() ? "host" : name) + "> " + text);
    }
    e.destruct();
}
