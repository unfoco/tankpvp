#pragma once

#include <flecs.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "protocol.h"
#include "util/serialize.h"

struct NetworkRegistry {
    enum class FieldKind : uint8_t {
        Bool,
        U8,
        U16,
        U32,
        U64,
        I8,
        I16,
        I32,
        I64,
        F32,
        F64,
    };

    struct Field {
        FieldKind kind = FieldKind::F32;
        uint32_t offset = 0;
        uint32_t count = 1;
        uint32_t size = 4;
        bool real = false;
        float quantum = 0;
        uint8_t bytes = 4;
    };

    struct Component {
        uint16_t id = 0;
        flecs::entity_t entity = 0;
        std::string name;
        uint32_t size = 0;
        uint16_t wire = 0;
        bool tag = false;
        std::vector<Field> fields;
    };

    std::vector<Component> components;
    std::unordered_map<std::string, uint16_t> ids;

    [[nodiscard]] auto find(uint16_t id) const -> const Component* {
        if (id == 0 || id > components.size()) {
            return nullptr;
        }
        return &components[id - 1];
    }

    void build(flecs::world& world);
    static void write(flecs::world& world, flecs::entity e, const Component& c, Writer& out);
    static void read(flecs::world& world, flecs::entity e, const Component& c, Reader& in);
    static void decode(const Component& c, void* dst, Reader& in);

    [[nodiscard]] auto describe() const -> std::vector<MessageComponentDescriptor>;
    void adopt(flecs::world& world, const std::vector<MessageComponentDescriptor>& descs, std::unordered_map<uint16_t, uint16_t>& remap);

   private:
    static auto register_runtime(flecs::world& world, const std::string& name, std::vector<Field>& fields, uint32_t& out_size) -> flecs::entity_t;
};
