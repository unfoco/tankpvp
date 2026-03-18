#include "network.h"

#include "component/network.h"

Network::Network(flecs::world& world) {
    world.set<NetworkTarget>({});
}
