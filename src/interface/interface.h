#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <clay.h>
#include <flecs.h>

#include <cfloat>
#include <deque>
#include <string>

#include "component/event.h"
#include "component/interface.h"
#include "util/fixed_buffer.h"

inline auto Str(const char* s) -> Clay_String {
    return {.length = static_cast<int32_t>(strlen(s)), .chars = s};
}
inline auto Str(const char* s, int n) -> Clay_String {
    return {.length = n, .chars = s};
}
inline auto Str(const std::string& s) -> Clay_String {
    return {.length = static_cast<int32_t>(s.size()), .chars = s.c_str()};
}

struct InputField {
    std::string editBuf;
    Clay_BoundingBox bounds = {};

    size_t cursor = 0;
    size_t anchor = 0;

    float scrollX = 0;
    float scrollY = 0;

    double blinkBase = 0;

    [[nodiscard]] auto hasSelection() const -> bool {
        return cursor != anchor;
    }
    [[nodiscard]] auto selectionStart() const -> size_t {
        return std::min(cursor, anchor);
    }
    [[nodiscard]] auto selectionEnd() const -> size_t {
        return std::max(cursor, anchor);
    }
    void collapseSelection() {
        anchor = cursor;
    }
};

enum class InterfaceCursor : uint8_t { Default, Pointer, Text };

struct InterfaceState {
    struct FieldSlot {
        uint32_t id = 0;
        InputField field;
    };

    InterfaceCursor cursor = InterfaceCursor::Default;
    InterfaceCursor cursorApplied = InterfaceCursor::Default;
    SDL_Cursor* cursors[3] = {};

    bool mousePressed = false;
    bool mouseDown = false;
    float mouseX = 0;
    float mouseY = 0;

    uint32_t activeId = 0;
    uint32_t focusedId = 0;
    uint32_t prevFocusedId = 0;
    bool focusConsumed = false;

    TTF_Font* font = nullptr;

    std::deque<std::string> textPool;

    auto intern(std::string s) -> const std::string& {
        textPool.push_back(std::move(s));
        return textPool.back();
    }

    FixedBuffer<FieldSlot, 64> fields;

    auto acquireField(uint32_t id) -> InputField& {
        for (auto& slot : fields) {
            if (slot.id == id) {
                return slot.field;
            }
        }
        fields.push({.id = id, .field = {}});
        return fields.data[fields.count - 1].field;
    }

    auto findField(uint32_t id) -> InputField* {
        for (auto& slot : fields) {
            if (slot.id == id) {
                return &slot.field;
            }
        }
        return nullptr;
    }
};

struct ButtonStyle {
    Clay_Color color = {.r = 50, .g = 50, .b = 55, .a = 255};
    Clay_Color textColor = {.r = 255, .g = 255, .b = 255, .a = 255};
    uint16_t fontSize = 32;
    float cornerRadius = 6;
    float width = 0;
    Clay_Padding padding = {.left = 16, .right = 16, .top = 10, .bottom = 10};
};

struct ToggleStyle {
    Clay_Color offColor = {.r = 80, .g = 80, .b = 85, .a = 255};
    Clay_Color onColor = {.r = 70, .g = 130, .b = 255, .a = 255};
    Clay_Color knobColor = {.r = 255, .g = 255, .b = 255, .a = 255};
    float width = 44;
    float height = 24;
    float knobPad = 3;
};

struct SliderStyle {
    Clay_Color trackColor = {.r = 50, .g = 50, .b = 55, .a = 255};
    Clay_Color fillColor = {.r = 70, .g = 130, .b = 255, .a = 255};
    Clay_SizingAxis width = CLAY_SIZING_GROW();
    float trackHeight = 12;
};

struct InputStyle {
    Clay_Color textColor = {.r = 255, .g = 255, .b = 255, .a = 255};
    Clay_Color placeholderColor = {.r = 255, .g = 255, .b = 255, .a = 100};
    Clay_Color bgColor = {.r = 35, .g = 35, .b = 40, .a = 255};
    Clay_Color borderColor = {.r = 80, .g = 80, .b = 85, .a = 255};
    Clay_Color focusBorderColor = {.r = 70, .g = 130, .b = 255, .a = 255};
    uint16_t borderWidth = 1;
    uint16_t fontSize = 32;
    float cornerRadius = 4;
    Clay_Sizing sizing = {};
    Clay_Padding padding = {.left = 8, .right = 8, .top = 6, .bottom = 6};
};

struct InputFilter {
    static auto Unsigned(char c) -> bool {
        return c >= '0' && c <= '9';
    }
    static auto Signed(char c) -> bool {
        return (c >= '0' && c <= '9') || c == '-';
    }
    static auto Float(char c) -> bool {
        return (c >= '0' && c <= '9') || c == '.' || c == '-';
    }
    static auto Hex(char c) -> bool {
        return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'f');
    }
    static auto Alphanum(char c) -> bool {
        return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
    }
    static auto Printable(char c) -> bool {
        return static_cast<unsigned char>(c) >= 0x80 || (c >= 32 && c < 127);
    }
    static auto Name(char c) -> bool {
        return static_cast<unsigned char>(c) >= 0x80 || (c > 32 && c < 127);
    }
    static auto Address(char c) -> bool {
        return Alphanum(c) || c == '.' || c == '-' || c == ':';
    }
};

struct InputConfig {
    float min = -FLT_MAX;
    float max = FLT_MAX;

    size_t maxLength = 0;
    size_t maxHeight = 0;
    size_t maxLine = 0;

    bool multiline = false;
    bool commitOnEnter = true;
    bool disabled = false;
    bool allowFormatting = false;

    const char* placeholder = "";
    const char* format = nullptr;

    bool (*allow)(char) = nullptr;
    bool (*validate)(const std::string&) = nullptr;
};

struct Interface {
    Interface(flecs::world& world);

   private:
    static void frame(flecs::iter& it, size_t i, InterfaceState& state);
    static void event(flecs::iter& it, size_t i, InterfaceState& state, const WindowEvents& events);
    static void build(flecs::iter& it, size_t i, InterfaceState& state, InterfaceCommands& cmds, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events);

   public:
    static auto button(InterfaceState& state, Clay_ElementId id, const char* label, ButtonStyle style = {}) -> bool;
    static auto toggle(InterfaceState& state, Clay_ElementId id, bool& value, ToggleStyle style = {}) -> bool;
    static auto slider(InterfaceState& state, Clay_ElementId id, float& value, float lo, float hi, SliderStyle style = {}) -> bool;

    template <typename T = std::string>
    static auto input(InterfaceState& state, const WindowEvents& events, Clay_ElementId id, T& value, InputConfig cfg = {}, InputStyle st = {}) -> bool;

    static auto wrap(InterfaceState& state, const std::string& text, uint16_t fontSize, float maxWidth) -> std::string;

    static auto main(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto host(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto pause(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto ingame(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto server(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto connect(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto settings(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto chat(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
    static auto status(flecs::iter& it, InterfaceState& state, InterfacePage& page, InterfacePrevious& prev, const WindowEvents& events) -> Clay_RenderCommandArray;
};
