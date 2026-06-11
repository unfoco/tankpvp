#pragma once

#include <enet/enet.h>

#include <cstdint>
#include <string>
#include <vector>

#include "component/input.h"
#include "util/serialize.h"

constexpr uint32_t NETWORK_PROTOCOL = 1;

constexpr uint16_t MAX_PLAYERS = 32;

constexpr uint32_t QUERY_MAGIC = 0x544E4B51;

constexpr uint8_t CHANNEL_RELIABLE = 0;
constexpr uint8_t CHANNEL_UNRELIABLE = 1;
constexpr uint8_t CHANNEL_ASSET = 2;
constexpr uint8_t CHANNEL_COUNT = 3;

constexpr uint32_t VIEW_MAX = 60;

constexpr float HIT_MARGIN_SERVER = 4.0F;

constexpr float CLAIM_MARGIN = 12.0F;

constexpr int CLAIM_REDUNDANCY = 10;

enum class Message : uint8_t {
    Ping = 0,
    Pong = 1,
    Ack = 2,
    Hello = 3,
    Welcome = 4,
    Registry = 5,
    CommandList = 6,
    Manifest = 7,
    AssetRequest = 8,
    AssetChunk = 9,
    Tileset = 10,
    TileChunk = 11,
    TileUnload = 12,
    TileSet = 13,
    Kick = 14,
    Chat = 15,
    Sound = 16,
    Hit = 17,
    Input = 18,
    Snapshot = 19,
    Structural = 20,
    ViewOpen = 21,
    ViewClose = 22,
    ViewEvent = 23,
    Effect = 24,
    Particles = 25,
};

namespace wire {

inline auto message(Message kind) -> serialize::Writer {
    serialize::Writer w;
    w.put(static_cast<uint8_t>(kind));
    return w;
}

inline void send(ENetPeer* peer, const serialize::Writer& w, uint8_t channel, bool reliable) {
    if (peer == nullptr) {
        return;
    }
    ENetPacket* packet = enet_packet_create(w.data.data(), w.data.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (packet == nullptr) {
        return;
    }
    if (enet_peer_send(peer, channel, packet) < 0) {
        enet_packet_destroy(packet);
    }
}

}

struct MessageFieldDescriptor {
    std::string name;
    uint8_t kind = 0;
    uint16_t count = 1;
    float quantum = 0;
    uint8_t bytes = 4;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(name);
        a & kind;
        a & count;
        a & quantum;
        a & bytes;
    }
};

struct MessageComponentDescriptor {
    uint16_t id = 0;
    std::string name;
    uint16_t wire_size = 0;
    uint8_t tag = 0;
    std::vector<MessageFieldDescriptor> fields;
    template <class Archive>
    void serialize(Archive& a) {
        a & id;
        a.text(name);
        a & wire_size;
        a & tag;
        a.template vector<uint8_t>(fields);
    }
};

struct MessageComponentData {
    uint16_t server_id = 0;
    std::vector<uint8_t> bytes;
    template <class Archive>
    void serialize(Archive& a) {
        a & server_id;
        a.template blob<uint16_t>(bytes);
    }
};

struct MessageEntity {
    uint64_t network_id = 0;
    std::vector<MessageComponentData> components;
    template <class Archive>
    void serialize(Archive& a) {
        a & network_id;
        a.template vector<uint8_t>(components);
    }
};

struct MessageInputCommand {
    uint16_t tick_delta = 0;
    uint32_t flags = 0;
    uint32_t prediction = 0;
    uint32_t view = 0;
    float muzzle_x = 0, muzzle_y = 0, aim = 0;
    template <class Archive>
    void serialize(Archive& a) {
        a & tick_delta;
        a & flags;
        if (flags & static_cast<uint32_t>(InputFlags::Shoot)) {
            a & prediction;
            a & view;
            a & muzzle_x;
            a & muzzle_y;
            a & aim;
        }
    }
};

struct MessageHitClaim {
    uint32_t prediction = 0;
    uint64_t target = 0;
    float x = 0, y = 0;
    template <class Archive>
    void serialize(Archive& a) {
        a & prediction;
        a & target;
        a & x;
        a & y;
    }
};

struct MessageCommandArg {
    std::string name;
    std::string type;
    uint8_t optional = 0;
    std::vector<std::string> values;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(name);
        a.text(type);
        a & optional;
        a.template strings<uint8_t>(values);
    }
};

struct MessageCommandInfo {
    std::string name;
    std::string description;
    std::vector<MessageCommandArg> arguments;
    std::vector<MessageCommandInfo> subcommands;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(name);
        a.text(description);
        a.template vector<uint8_t>(arguments);
        a.template vector<uint8_t>(subcommands);
    }
};

struct MessageBlip {
    float x = 0;
    float y = 0;
    float radius = 0;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
    template <class Archive>
    void serialize(Archive& ar) {
        ar & x;
        ar & y;
        ar & radius;
        ar & r;
        ar & g;
        ar & b;
        ar & a;
    }
};

struct MessageViewWidget {
    uint8_t kind = 0;
    uint8_t layout = 0;
    uint8_t card = 0;
    std::string text;
    uint32_t handler = 0;
    std::string bind;
    float number = 0;
    std::string bind_max;
    float number_max = 1;
    uint8_t color_r = 80;
    uint8_t color_g = 200;
    uint8_t color_b = 120;
    uint8_t bg_r = 24;
    uint8_t bg_g = 24;
    uint8_t bg_b = 32;
    uint8_t bg_a = 240;
    std::string field;
    std::vector<MessageBlip> blips;
    std::vector<MessageViewWidget> children;
    template <class Archive>
    void serialize(Archive& a) {
        a & kind;
        a & layout;
        a & card;
        a.text(text);
        a & handler;
        a.text(bind);
        a & number;
        a.text(bind_max);
        a & number_max;
        a & color_r;
        a & color_g;
        a & color_b;
        a & bg_r;
        a & bg_g;
        a & bg_b;
        a & bg_a;
        a.text(field);
        a.template vector<uint8_t>(blips);
        a.template vector<uint8_t>(children);
    }
};

struct MessageViewValue {
    std::string key;
    std::string value;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(key);
        a.text(value);
    }
};

struct MessageAssetEntry {
    std::string name;
    uint64_t hash = 0;
    uint8_t kind = 0;
    uint32_t size = 0;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(name);
        a & hash;
        a & kind;
        a & size;
    }
};

struct MessagePing {
    uint64_t token = 0;
    template <class Archive>
    void serialize(Archive& a) {
        a & token;
    }
};

struct MessagePong {
    uint32_t protocol = 0;
    uint64_t token = 0;
    uint16_t players = 0;
    uint16_t max_players = 0;
    uint16_t tickrate = 0;
    template <class Archive>
    void serialize(Archive& a) {
        a & protocol;
        a & token;
        a & players;
        a & max_players;
        a & tickrate;
    }
};

struct MessageAcknowledge {
    uint64_t tick = 0;
    template <class Archive>
    void serialize(Archive& a) {
        a & tick;
    }
};

struct MessageHello {
    std::string username;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(username);
    }
};

struct MessageWelcome {
    uint32_t protocol = 0;
    uint32_t peer_id = 0;
    uint64_t controlled_entity = 0;
    uint64_t tick = 0;
    uint16_t tickrate = 0;
    uint16_t registry_version = 0;
    std::vector<MessageComponentDescriptor> components;
    template <class Archive>
    void serialize(Archive& a) {
        a & protocol;
        a & peer_id;
        a & controlled_entity;
        a & tick;
        a & tickrate;
        a & registry_version;
        a.template vector<uint16_t>(components);
    }
};

struct MessageRegistry {
    uint16_t registry_version = 0;
    std::vector<MessageComponentDescriptor> components;
    template <class Archive>
    void serialize(Archive& a) {
        a & registry_version;
        a.template vector<uint16_t>(components);
    }
};

struct MessageCommandList {
    std::vector<MessageCommandInfo> commands;
    template <class Archive>
    void serialize(Archive& a) {
        a.template vector<uint16_t>(commands);
    }
};

struct MessageManifest {
    uint16_t version = 0;
    std::vector<MessageAssetEntry> entries;
    template <class Archive>
    void serialize(Archive& a) {
        a & version;
        a.template vector<uint16_t>(entries);
    }
};

struct MessageAssetRequest {
    std::vector<uint64_t> hashes;
    template <class Archive>
    void serialize(Archive& a) {
        a.template vector<uint16_t>(hashes);
    }
};

struct MessageAssetChunk {
    uint64_t hash = 0;
    uint32_t offset = 0;
    uint32_t total = 0;
    std::vector<uint8_t> bytes;
    template <class Archive>
    void serialize(Archive& a) {
        a & hash;
        a & offset;
        a & total;
        a.template blob<uint16_t>(bytes);
    }
};

struct MessageTileType {
    uint64_t texture = 0;
    uint8_t solid = 1;
    float restitution = 0.0F;
    float friction = 0.5F;
    float drag = 0.0F;
    int32_t hp = 0;
    template <class Archive>
    void serialize(Archive& a) {
        a & texture;
        a & solid;
        a & restitution;
        a & friction;
        a & drag;
        a & hp;
    }
};

struct MessageTileset {
    std::vector<MessageTileType> types;
    template <class Archive>
    void serialize(Archive& a) {
        a.template vector<uint16_t>(types);
    }
};

struct MessageTileChunk {
    int32_t cx = 0;
    int32_t cy = 0;
    std::vector<uint16_t> tiles;
    template <class Archive>
    void serialize(Archive& a) {
        a & cx;
        a & cy;
        a.template vector<uint16_t>(tiles);
    }
};

struct MessageTileUnload {
    int32_t cx = 0;
    int32_t cy = 0;
    template <class Archive>
    void serialize(Archive& a) {
        a & cx;
        a & cy;
    }
};

struct MessageTileSet {
    int32_t tx = 0;
    int32_t ty = 0;
    uint16_t id = 0;
    template <class Archive>
    void serialize(Archive& a) {
        a & tx;
        a & ty;
        a & id;
    }
};

struct MessageKick {
    std::string reason;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(reason);
    }
};

struct MessageChat {
    std::string text;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(text);
    }
};

struct MessageSound {
    std::string asset;
    float x = 0;
    float y = 0;
    float volume = 1.0F;
    bool global = false;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(asset);
        a & x;
        a & y;
        a & volume;
        a & global;
    }
};

struct MessageEffect {
    float x = 0;
    float y = 0;
    float angle = 0;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    template <class Archive>
    void serialize(Archive& a) {
        a & x;
        a & y;
        a & angle;
        a & r;
        a & g;
        a & b;
    }
};

struct MessageParticles {
    float x = 0;
    float y = 0;
    float dir = 0;
    float spread = 6.2832F;
    uint16_t count = 12;
    uint64_t texture = 0;
    float speed_min = 40;
    float speed_max = 120;
    float size_min = 8;
    float size_max = 18;
    float life_min = 0.5F;
    float life_max = 1.0F;
    float gravity = 0;
    float drag = 1.5F;
    float spin = 9.0F;
    float grow = 0;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t alpha = 255;
    uint8_t additive = 0;
    template <class Archive>
    void serialize(Archive& a) {
        a & x;
        a & y;
        a & dir;
        a & spread;
        a & count;
        a & texture;
        a & speed_min;
        a & speed_max;
        a & size_min;
        a & size_max;
        a & life_min;
        a & life_max;
        a & gravity;
        a & drag;
        a & spin;
        a & grow;
        a & r;
        a & g;
        a & b;
        a & alpha;
        a & additive;
    }
};

struct MessageHit {
    std::vector<MessageHitClaim> claims;
    template <class Archive>
    void serialize(Archive& a) {
        a.template vector<uint8_t>(claims);
    }
};

struct MessageInput {
    uint64_t newest_tick = 0;
    double send_time = 0;
    std::vector<MessageInputCommand> commands;
    template <class Archive>
    void serialize(Archive& a) {
        a & newest_tick;
        a & send_time;
        a.template vector<uint8_t>(commands);
    }
};

struct MessageSnapshot {
    uint64_t tick = 0;
    uint64_t acknowledged_tick = 0;
    uint32_t input_buffer = 0;
    double send_time = 0;
    uint16_t registry_version = 0;
    std::vector<MessageEntity> deltas;
    template <class Archive>
    void serialize(Archive& a) {
        a & tick;
        a & acknowledged_tick;
        a & input_buffer;
        a & send_time;
        a & registry_version;
        a.template vector<uint16_t>(deltas);
    }
};

struct MessageStructural {
    uint64_t tick = 0;
    std::vector<MessageEntity> spawns;
    std::vector<uint64_t> despawns;
    template <class Archive>
    void serialize(Archive& a) {
        a & tick;
        a.template vector<uint16_t>(spawns);
        a.template vector<uint16_t>(despawns);
    }
};

struct MessageViewOpen {
    std::string id;
    uint8_t placement = 0;
    MessageViewWidget root;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(id);
        a & placement;
        a & root;
    }
};

struct MessageViewClose {
    std::string id;
    template <class Archive>
    void serialize(Archive& a) {
        a.text(id);
    }
};

struct MessageViewEvent {
    uint32_t handler = 0;
    std::vector<MessageViewValue> values;
    template <class Archive>
    void serialize(Archive& a) {
        a & handler;
        a.template vector<uint8_t>(values);
    }
};
