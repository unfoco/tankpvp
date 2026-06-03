#pragma once

#include <clay.h>

#include <cstdint>

enum class InterfacePage : std::uint8_t {
    None,
    Main,
    Host,
    Pause,
    Ingame,
    Connect,
    Settings,
};

struct InterfacePrevious {
    InterfacePage page;
};

struct InterfaceCommands {
    Clay_RenderCommandArray list;
};
