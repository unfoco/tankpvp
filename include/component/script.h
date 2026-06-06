#pragma once

#include <flecs.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct CommandSender {
    flecs::entity peer;
    flecs::entity tank;
    std::string name;
    bool admin = false;
};

struct CommandArgument {
    std::string name;
    std::string type;
    bool optional = false;
    std::vector<std::string> values;
};

struct CommandInfo {
    std::string name;
    std::string description;
    std::vector<CommandArgument> arguments;
    std::vector<CommandInfo> subcommands;
};

struct CommandBook {
    std::vector<CommandInfo> commands;
};

enum class ViewKind : uint8_t {
    Panel,
    Label,
    Button,
    Bar,
    Input,
    Spacer,
    Separator,
    Slider,
    Toggle,
};

enum class ViewPlacement : uint8_t {
    Center,
    Hud,
};

enum class ViewLayout : uint8_t {
    Column,
    Row,
};

struct ViewWidget {
    ViewKind kind = ViewKind::Label;
    ViewLayout layout = ViewLayout::Column;
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
    std::vector<ViewWidget> children;
};

struct ViewActive {
    std::string id;
    ViewPlacement placement = ViewPlacement::Center;
    ViewWidget root;
    std::unordered_map<std::string, std::string> values;
};

struct ViewState {
    std::vector<ViewActive> views;
    uint32_t clicked = 0;
    std::string clicked_view;
};

struct RequestCommand {
    CommandSender sender;
    std::string text;
};

struct RequestView {
    flecs::entity peer;
    std::string id;
    bool close = false;
    ViewPlacement placement = ViewPlacement::Center;
    ViewWidget root;
};

struct RequestViewInteraction {
    CommandSender sender;
    uint32_t handler = 0;
    std::vector<std::pair<std::string, std::string>> values;
};

struct RequestViewClick {
    uint32_t handler = 0;
    std::vector<std::pair<std::string, std::string>> values;
};

struct RequestPlayerJoin {
    flecs::entity peer;
    std::string username;
};
