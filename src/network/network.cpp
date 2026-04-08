#include "network.h"

#include "SDL3/SDL_log.h"
#include "component/network.h"

Network::Network(flecs::world& world) {
    if (enet_initialize() != 0) {
        SDL_Log("An error occurred while initializing ENet.");
        exit(EXIT_FAILURE);
    }
    atexit(enet_deinitialize);

    world.set<NetworkTarget>({});
}

void Network::host(flecs::entity, const NetworkRequestHost& target) {
    auto address = ENetAddress{
        .port = target.port
    };
    enet_address_set_host(&address, target.address.c_str());

    auto server = enet_host_create(&address, 32, 2, 0, 0);
    if (!server) {
        SDL_Log("An error occurred while trying to create an ENet server host.");
        exit(EXIT_FAILURE);
    }
}

void Network::join(flecs::entity, const NetworkRequestJoin&) {
    auto client = enet_host_create(NULL, 1, 2, 0, 0);

    if (!client) {
        SDL_Log("An error occurred while trying to create an ENet client host.");
        exit (EXIT_FAILURE);
    }
}

void Network::quit(flecs::entity, const NetworkRequestQuit&) {
    //enet_host_destroy(server);
    //enet_host_destroy(client);
}
