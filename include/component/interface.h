#pragma once

#include <clay.h>

#include <cstdint>
#include <string>
#include <vector>

#include "util/time.h"

enum class InterfacePage : std::uint8_t {
    None,
    Main,
    Host,
    Chat,
    Pause,
    Ingame,
    Server,
    Connect,
    Settings,
    Content,
    Status,
};

struct InterfacePrevious {
    InterfacePage page;
};

struct InterfaceTransition {
    InterfacePage shown = InterfacePage::None;
    std::vector<InterfacePage> history;
};

struct ContentLoad {
    bool showing = false;
    bool in_session = false;
    double shown_since = 0;
    double busy_since = 0;
};

struct InterfaceCommands {
    Clay_RenderCommandArray list;
};

struct ChatLog {
    static constexpr int CAP = 64;
    std::string lines[CAP];
    double times[CAP] = {};
    int count = 0;
    int head = 0;

    void push(const std::string& line) {
        lines[head] = line;
        times[head] = util::now();
        head = (head + 1) % CAP;
        if (count < CAP) {
            count++;
        }
    }
    [[nodiscard]] auto at(int i) const -> const std::string& {
        return lines[(head - count + i + (2 * CAP)) % CAP];
    }
    [[nodiscard]] auto time(int i) const -> double {
        return times[(head - count + i + (2 * CAP)) % CAP];
    }
};

struct ChatInput {
    std::string draft;
    float scroll = 0;
    std::vector<std::string> sent;
    int history_index = -1;
    std::string stash;
    int complete = 0;
};

struct InputCapture {
    bool active = false;
};

struct MinimapHandle {
    void* image = nullptr;
    float size = 0;
    float range = 0;
};

struct ServerEntry {
    std::string name;
    std::string address = "127.0.0.1";
    uint16_t port = 5000;

    auto operator==(const ServerEntry&) const -> bool = default;
};

struct ServerList {
    std::vector<ServerEntry> entries;
    float scroll = 0;
    int editing = -1;
    ServerEntry draft;
};
