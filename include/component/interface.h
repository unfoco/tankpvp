#pragma once

#include <clay.h>

enum class InterfacePage {
    None,
    Main,
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
