#pragma once

#include <enet/enet.h>

#include <cstdint>
#include <string>
#include <vector>

#include "component/input.h"
#include "util/serialize.h"

constexpr uint32_t NETWORK_PROTOCOL = 11;

constexpr uint8_t CHANNEL_RELIABLE   = 0;
constexpr uint8_t CHANNEL_UNRELIABLE = 1;
constexpr uint8_t CHANNEL_COUNT      = 2;

constexpr uint32_t VIEW_MAX = 60;

constexpr float HIT_MARGIN_SERVER = 4.0f;

constexpr float CLAIM_MARGIN = 12.0f;

constexpr int CLAIM_REDUNDANCY = 4;

enum class Message : uint8_t {
    Welcome,
    Snapshot,
    Structural,
    Input,
    Ack,
    Hit,
};

namespace wire {

inline Writer message(Message kind) { Writer w; w.put(static_cast<uint8_t>(kind)); return w; }

inline void send(ENetPeer* peer, const Writer& w, uint8_t channel, bool reliable) {
    ENetPacket* packet = enet_packet_create(w.data.data(), w.data.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(peer, channel, packet);
}

}

struct MessageFieldDescriptor {
    uint8_t  kind = 0;
    uint16_t count = 1;
    float    quantum = 0;
    uint8_t  bytes = 4;
    template<class Archive> void serialize(Archive& a) { a & kind; a & count; a & quantum; a & bytes; }
};

struct MessageComponentDescriptor {
    uint16_t    id = 0;
    std::string name;
    uint16_t    wire_size = 0;
    uint8_t     tag = 0;
    std::vector<MessageFieldDescriptor> fields;
    template<class Archive> void serialize(Archive& a) {
        a & id; a.text(name); a & wire_size; a & tag;
        a.template vector<uint8_t>(fields);
    }
};

struct MessageWelcome {
    uint32_t protocol = 0;
    uint32_t peer_id = 0;
    uint64_t controlled_entity = 0;
    uint64_t tick = 0;
    uint16_t tickrate = 0;
    std::vector<MessageComponentDescriptor> components;
    template<class Archive> void serialize(Archive& a) {
        a & protocol; a & peer_id; a & controlled_entity; a & tick; a & tickrate;
        a.template vector<uint16_t>(components);
    }
};

struct MessageComponentData {
    uint16_t server_id = 0;
    std::vector<uint8_t> bytes;
    template<class Archive> void serialize(Archive& a) { a & server_id; a.template blob<uint16_t>(bytes); }
};

struct MessageEntity {
    uint64_t network_id = 0;
    std::vector<MessageComponentData> components;
    template<class Archive> void serialize(Archive& a) { a & network_id; a.template vector<uint8_t>(components); }
};

struct MessageSnapshot {
    uint64_t tick = 0;
    uint64_t acknowledged_tick = 0;
    uint32_t input_buffer = 0;
    double   send_time = 0;
    std::vector<MessageEntity> deltas;
    template<class Archive> void serialize(Archive& a) {
        a & tick; a & acknowledged_tick; a & input_buffer; a & send_time;
        a.template vector<uint16_t>(deltas);
    }
};

struct MessageStructural {
    uint64_t tick = 0;
    std::vector<MessageEntity> spawns;
    std::vector<uint64_t>      despawns;
    template<class Archive> void serialize(Archive& a) {
        a & tick;
        a.template vector<uint16_t>(spawns);
        a.template vector<uint16_t>(despawns);
    }
};

struct MessageInputCommand {
    uint16_t tick_delta = 0;
    uint32_t flags = 0;
    uint32_t prediction = 0;
    uint32_t view = 0;
    float    muzzle_x = 0, muzzle_y = 0, aim = 0;
    template<class Archive> void serialize(Archive& a) {
        a & tick_delta; a & flags;
        if (flags & static_cast<uint32_t>(InputFlags::Shoot)) {
            a & prediction; a & view; a & muzzle_x; a & muzzle_y; a & aim;
        }
    }
};

struct MessageInput {
    uint64_t newest_tick = 0;
    double   send_time = 0;
    std::vector<MessageInputCommand> commands;
    template<class Archive> void serialize(Archive& a) {
        a & newest_tick; a & send_time;
        a.template vector<uint8_t>(commands);
    }
};

struct MessageAcknowledge {
    uint64_t tick = 0;
    template<class Archive> void serialize(Archive& a) { a & tick; }
};

struct MessageHitClaim {
    uint32_t prediction = 0;
    uint64_t target = 0;
    template<class Archive> void serialize(Archive& a) { a & prediction; a & target; }
};

struct MessageHit {
    std::vector<MessageHitClaim> claims;
    template<class Archive> void serialize(Archive& a) { a.template vector<uint8_t>(claims); }
};
