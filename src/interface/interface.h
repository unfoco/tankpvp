#pragma once

#include <cfloat>
#include <string>

#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3/SDL.h>
#include <flecs.h>
#include <clay.h>

#include "type/fixed_buffer.h"

#include "component/event.h"
#include "component/interface.h"

inline Clay_String Str(const char* s)        { return {.length = (int32_t)strlen(s), .chars = s}; }
inline Clay_String Str(const char* s, int n) { return {.length = n, .chars = s}; }
inline Clay_String Str(const std::string& s) { return {.length = (int32_t)s.size(), .chars = s.c_str()}; }

struct InputField {
    std::string editBuf;
    Clay_BoundingBox bounds = {};

    size_t cursor = 0;
    size_t anchor = 0;

    float scrollX = 0;
    float scrollY = 0;

    bool hasSelection()      const { return cursor != anchor; }
    size_t selectionStart()  const { return std::min(cursor, anchor); }
    size_t selectionEnd()    const { return std::max(cursor, anchor); }
    void collapseSelection()       { anchor = cursor; }
};

struct InterfaceState {
    struct FieldSlot { uint32_t id = 0; InputField field; };

    bool mousePressed  = false;
    bool mouseDown     = false;
    float mouseX       = 0;
    float mouseY       = 0;

    uint32_t activeId      = 0;
    uint32_t focusedId     = 0;
    uint32_t prevFocusedId = 0;
    bool focusConsumed     = false;

    float dragOriginX     = 0;
    float dragOriginValue = 0;

    TTF_Font* font = nullptr;

    FixedBuffer<FieldSlot, 64> fields;

    InputField& acquireField(uint32_t id) {
        for (auto& slot : fields) if (slot.id == id) return slot.field;
        fields.push({id, {}});
        return fields.data[fields.count - 1].field;
    }

    InputField* findField(uint32_t id) {
        for (auto& slot : fields) if (slot.id == id) return &slot.field;
        return nullptr;
    }
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
    Clay_Color   bgColor          = {35, 35, 40, 255};
    Clay_Color   borderColor      = {80, 80, 85, 255};
    Clay_Color   focusBorderColor = {70, 130, 255, 255};
    uint16_t     borderWidth      = 1;
    uint16_t     fontSize         = 16;
    Clay_Sizing  sizing           = {};
    Clay_Padding padding          = {8, 8, 6, 6};
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
    float min = -FLT_MAX;
    float max =  FLT_MAX;

    size_t maxLength = 0;
    size_t maxHeight = 0;
    size_t maxLine   = 0;

    bool multiline     = false;
    bool commitOnEnter = true;

    const char* placeholder = "";
    const char* format      = nullptr;

    bool (*allow)(char)                  = nullptr;
    bool (*validate)(const std::string&) = nullptr;
};

struct Interface {
    Interface(flecs::world&);

private:
    static void frame(flecs::iter&, size_t, InterfaceState&);
    static void event(flecs::iter&, size_t, InterfaceState&, const WindowEvents&);
    static void build(flecs::iter&, size_t, InterfaceState&, InterfaceCommands&, InterfacePage&, InterfacePrevious&, const WindowEvents&);

public:
    static bool button(InterfaceState&, Clay_ElementId, const char* label, ButtonStyle = {});
    static bool toggle(InterfaceState&, Clay_ElementId, bool& value, ToggleStyle = {});
    static bool slider(InterfaceState&, Clay_ElementId, float& value, float lo, float hi, SliderStyle = {});

    template<typename T = std::string>
    static bool input(InterfaceState&, const WindowEvents&, Clay_ElementId, T&, InputConfig = {}, InputStyle = {});

    static Clay_RenderCommandArray main(flecs::iter&, InterfaceState&, InterfacePage&, InterfacePrevious&, const WindowEvents&);
    static Clay_RenderCommandArray pause(flecs::iter&, InterfaceState&, InterfacePage&, InterfacePrevious&, const WindowEvents&);
    static Clay_RenderCommandArray ingame(flecs::iter&, InterfaceState&, InterfacePage&, InterfacePrevious&, const WindowEvents&);
    static Clay_RenderCommandArray connect(flecs::iter&, InterfaceState&, InterfacePage&, InterfacePrevious&, const WindowEvents&);
    static Clay_RenderCommandArray settings(flecs::iter&, InterfaceState&, InterfacePage&, InterfacePrevious&, const WindowEvents&);
};
