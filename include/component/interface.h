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
    Pause,
    Ingame,
    Server,
    Connect,
    Settings,
    Chat,
    Status,
};

struct InterfacePrevious {
    InterfacePage page;
};

enum class TransitionKind : std::uint8_t { Crossfade, Slide };
enum class TransitionDir : std::uint8_t { Left, Right, Up, Down };

struct InterfaceTransition {
    InterfacePage shown = InterfacePage::None;
    double start = 0;
    double duration = 0.18;
    TransitionKind kind = TransitionKind::Crossfade;
    TransitionDir dir = TransitionDir::Left;
    InterfacePage lastFrom = InterfacePage::None;
    InterfacePage lastTo = InterfacePage::None;
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
    int historyIndex = -1;
    std::string stash;
};

struct InputCapture {
    bool active = false;
};

struct ServerEntry {
    std::string name;
    std::string address = "127.0.0.1";
    uint16_t port = 5000;
};

struct ServerList {
    std::vector<ServerEntry> entries;
    float scroll = 0;
    int editing = -1;
    ServerEntry draft;
};
