#pragma once

#include <cstdint>

enum class InputFlags : std::uint8_t {
    None     = 0,
    Backward = 1 << 0,
    Forward  = 1 << 1,
    Left     = 1 << 2,
    Right    = 1 << 3,
    Shoot    = 1 << 4,
};

inline InputFlags operator~(InputFlags a) {
    return static_cast<InputFlags>(~static_cast<uint8_t>(a));
}

inline InputFlags operator&(InputFlags a, InputFlags b) {
    return static_cast<InputFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline InputFlags operator|(InputFlags a, InputFlags b) {
    return static_cast<InputFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline InputFlags& operator|=(InputFlags& a, InputFlags b) {
    return a = a | b;
}
