#pragma once

#include <cfloat>
#include <string>
#include <unordered_map>

#include <SDL3/SDL.h>
#include <flecs.h>
#include <clay.h>

#include "component/event.h"
#include "component/interface.h"

struct InterfaceState {
    bool mousePressed = false;
    bool mouseDown = false;

    float mouseX = 0;
    float mouseY = 0;

    uint32_t activeId = 0;
    uint32_t focusedId = 0;
    uint32_t focusedLastId = 0;

    float dragStartX = 0;
    float dragStartVal = 0;

    std::unordered_map<uint32_t, std::string> editBuffers;
};

struct ButtonStyle {
    Clay_Color   color        = {50, 50, 55, 255};
    Clay_Color   textColor    = {255, 255, 255, 255};
    uint16_t     fontSize     = 16;
    float        cornerRadius = 6;
    Clay_Padding padding      = {16, 16, 10, 10};
};

struct ToggleStyle {
    Clay_Color offColor  = {80, 80, 85, 255};
    Clay_Color onColor   = {70, 130, 255, 255};
    Clay_Color knobColor = {255, 255, 255, 255};
    float width   = 44;
    float height  = 24;
    float knobPad = 3;
};

struct SliderStyle {
    Clay_Color trackColor = {50, 50, 55, 255};
    Clay_Color fillColor  = {70, 130, 255, 255};
    float trackWidth  = 200;
    float trackHeight = 6;
};

struct InputStyle {
    Clay_Color   textColor        = {255, 255, 255, 255};
    Clay_Color   placeholderColor = {255, 255, 255, 100};
    Clay_Color   backgroundColor  = {35, 35, 40, 255};
    Clay_Color   borderColor      = {80, 80, 85, 255};
    Clay_Color   focusBorderColor = {70, 130, 255, 255};
    uint16_t     borderWidth      = 1;
    uint16_t     fontSize         = 16;
    Clay_Sizing  sizing           = {};
    Clay_Padding padding          = {8, 8, 6, 6};
    bool         indentNewLine    = false;
};

struct InputFilter {
    static bool Unsigned(char c)  { return c >= '0' && c <= '9'; }
    static bool Signed(char c)    { return (c >= '0' && c <= '9') || c == '-'; }
    static bool Float(char c)     { return (c >= '0' && c <= '9') || c == '.' || c == '-'; }
    static bool Hex(char c)       { return (c >= '0' && c <= '9') || ((c|32) >= 'a' && (c|32) <= 'f'); }
    static bool Alphanum(char c)  { return (c >= '0' && c <= '9') || ((c|32) >= 'a' && (c|32) <= 'z'); }
    static bool Printable(char c) { return (unsigned char)c >= 0x80 || (c >= 32 && c < 127); }
    static bool Address(char c)   { return Alphanum(c) || c == '.' || c == '-' || c == ':'; }
};

struct InputConfig {
    float  min = -FLT_MAX;
    float  max =  FLT_MAX;
    size_t len = 0;

    bool multiline     = false;
    bool commitOnEnter = true;

    const char* placeholder = "";
    const char* format      = nullptr;
    const char* pattern     = nullptr;

    bool (*allow)(char)                  = nullptr;
    bool (*validate)(const std::string&) = nullptr;
};

inline Clay_String Str(const char* s)        { return {.length = (int32_t)strlen(s), .chars = s}; }
inline Clay_String Str(const std::string& s) { return {.length = (int32_t)s.size(), .chars = s.c_str()}; }

struct Interface {
    Interface(flecs::world&);

private:
    static void frame(flecs::iter&, size_t, InterfaceState&);
    static void event(flecs::iter&, size_t, InterfaceState&, const WindowEvents&);
    static void build(flecs::iter&, size_t, InterfaceState&, InterfaceCommands&, InterfacePage&, InterfacePrevious&, const WindowEvents&);

public:
    static bool button(InterfaceState&, Clay_ElementId, const char* label, ButtonStyle = {});
    static bool toggle(InterfaceState&, Clay_ElementId, bool& value, ToggleStyle = {});
    static bool slider(InterfaceState&, Clay_ElementId, float& value, float low, float high, SliderStyle = {});

    template <typename T = std::string>
    static bool input(InterfaceState&, const WindowEvents&, Clay_ElementId, T&, InputConfig = {}, InputStyle = {});

    static Clay_RenderCommandArray main(flecs::iter&, InterfaceState&, InterfacePage&, InterfacePrevious&, const WindowEvents&);
    static Clay_RenderCommandArray pause(flecs::iter&, InterfaceState&, InterfacePage&, InterfacePrevious&, const WindowEvents&);
    static Clay_RenderCommandArray ingame(flecs::iter&, InterfaceState&, InterfacePage&, InterfacePrevious&, const WindowEvents&);
    static Clay_RenderCommandArray connect(flecs::iter&, InterfaceState&, InterfacePage&, InterfacePrevious&, const WindowEvents&);
    static Clay_RenderCommandArray settings(flecs::iter&, InterfaceState&, InterfacePage&, InterfacePrevious&, const WindowEvents&);
};
