#pragma once

#include <flecs.h>

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "util/serialize.h"

#include "protocol.h"

struct NetworkRegistry {
    enum class FieldKind : uint8_t {
        Bool, U8, U16, U32, U64, I8, I16, I32, I64, F32, F64,
    };

    struct Field {
        FieldKind kind  = FieldKind::F32;
        uint32_t offset  = 0;
        uint32_t count   = 1;
        uint32_t size    = 4;
        bool     real    = false;
        float    quantum = 0;
        uint8_t  bytes   = 4;
    };

    struct Component {
        uint16_t        id     = 0;
        flecs::entity_t entity = 0;
        std::string     name;
        uint32_t        size   = 0;
        uint16_t        wire   = 0;
        bool            tag    = false;
        std::vector<Field> fields;
    };

    std::vector<Component> components;
    std::unordered_map<std::string, uint16_t> ids;

    const Component* find(uint16_t id) const {
        if (id == 0 || id > components.size()) return nullptr;
        return &components[id - 1];
    }

    void build(flecs::world&);
    void write(flecs::world&, flecs::entity, const Component&, Writer&) const;
    void read(flecs::world&, flecs::entity, const Component&, Reader&) const;
    void decode(const Component&, void* dst, Reader&) const;

    std::vector<MessageComponentDescriptor> describe() const;
    void adopt(flecs::world&, const std::vector<MessageComponentDescriptor>&,
               std::unordered_map<uint16_t, uint16_t>& remap);

  private:
    flecs::entity_t register_runtime(flecs::world&, const std::string& name,
                                     std::vector<Field>& fields, uint32_t& out_size) const;
};
