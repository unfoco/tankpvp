#pragma once

#include <cstdint>

#include <flecs.h>

struct InputFlags : flecs::bitmask {
    static constexpr uint32_t None     = 0;
    static constexpr uint32_t Left     = 1 << 0;
    static constexpr uint32_t Right    = 1 << 1;
    static constexpr uint32_t Backward = 1 << 2;
    static constexpr uint32_t Forward  = 1 << 3;
    static constexpr uint32_t Shoot    = 1 << 4;

    InputFlags(uint32_t value = None) {
        this->value = value;
    }

    bool has(const uint32_t flag) const {
        return (this->value & flag) != InputFlags::None;
    }
};
