#include "registry.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

#include "component/network.h"

using FieldKind = NetworkRegistry::FieldKind;

static auto primitive_info(ecs_primitive_kind_t kind, uint32_t& size, bool& real) -> bool {
    switch (kind) {
        case EcsBool:
        case EcsChar:
        case EcsByte:
        case EcsU8:
        case EcsI8:
            size = 1;
            real = false;
            return true;
        case EcsU16:
        case EcsI16:
            size = 2;
            real = false;
            return true;
        case EcsU32:
        case EcsI32:
            size = 4;
            real = false;
            return true;
        case EcsU64:
        case EcsI64:
            size = 8;
            real = false;
            return true;
        case EcsF32:
            size = 4;
            real = true;
            return true;
        case EcsF64:
            size = 8;
            real = true;
            return true;
        default:
            return false;
    }
}

static auto kind_from_primitive(ecs_primitive_kind_t k) -> FieldKind {
    switch (k) {
        case EcsBool:
            return FieldKind::Bool;
        case EcsChar:
        case EcsByte:
        case EcsU8:
            return FieldKind::U8;
        case EcsI8:
            return FieldKind::I8;
        case EcsU16:
            return FieldKind::U16;
        case EcsI16:
            return FieldKind::I16;
        case EcsU32:
            return FieldKind::U32;
        case EcsI32:
            return FieldKind::I32;
        case EcsU64:
            return FieldKind::U64;
        case EcsI64:
            return FieldKind::I64;
        case EcsF32:
            return FieldKind::F32;
        case EcsF64:
            return FieldKind::F64;
        default:
            return FieldKind::U8;
    }
}

static void kind_info(FieldKind k, uint32_t& size, bool& real) {
    switch (k) {
        case FieldKind::Bool:
        case FieldKind::U8:
        case FieldKind::I8:
            size = 1;
            real = false;
            break;
        case FieldKind::U16:
        case FieldKind::I16:
            size = 2;
            real = false;
            break;
        case FieldKind::U32:
        case FieldKind::I32:
            size = 4;
            real = false;
            break;
        case FieldKind::U64:
        case FieldKind::I64:
            size = 8;
            real = false;
            break;
        case FieldKind::F32:
            size = 4;
            real = true;
            break;
        case FieldKind::F64:
            size = 8;
            real = true;
            break;
    }
}

static auto kind_primitive(FieldKind k) -> ecs_entity_t {
    switch (k) {
        case FieldKind::Bool:
            return ecs_id(ecs_bool_t);
        case FieldKind::U8:
            return ecs_id(ecs_u8_t);
        case FieldKind::U16:
            return ecs_id(ecs_u16_t);
        case FieldKind::U32:
            return ecs_id(ecs_u32_t);
        case FieldKind::U64:
            return ecs_id(ecs_u64_t);
        case FieldKind::I8:
            return ecs_id(ecs_i8_t);
        case FieldKind::I16:
            return ecs_id(ecs_i16_t);
        case FieldKind::I32:
            return ecs_id(ecs_i32_t);
        case FieldKind::I64:
            return ecs_id(ecs_i64_t);
        case FieldKind::F32:
            return ecs_id(ecs_f32_t);
        case FieldKind::F64:
            return ecs_id(ecs_f64_t);
    }
    return ecs_id(ecs_u8_t);
}

void NetworkRegistry::build(flecs::world& world) {
    components.clear();
    ids.clear();

    ecs_world_t* w = world.c_ptr();

    std::vector<flecs::entity> comps;
    world.query_builder().with<Networked>().build().run([&](flecs::iter& it) -> void {
        while (it.next()) {
            for (auto i : it) {
                comps.push_back(it.entity(i));
            }
        }
    });

    std::ranges::sort(comps, [](flecs::entity a, flecs::entity b) -> bool { return std::strcmp(a.name().c_str(), b.name().c_str()) < 0; });

    uint16_t id = 1;
    for (auto e : comps) {
        Component c;
        c.id = id;
        c.entity = e.id();
        c.name = e.name().c_str();

        float quantum = 0;
        uint8_t bytes = 4;
        if (const auto* q = e.try_get<Quantize>()) {
            quantum = q->precision;
            bytes = q->bytes;
        }

        const ecs_type_info_t* ti = ecs_get_type_info(w, c.entity);
        c.size = (ti != nullptr) ? static_cast<uint32_t>(ti->size) : 0;

        const EcsStruct* s = ecs_get(w, e, EcsStruct);
        if (s != nullptr) {
            int32_t n = ecs_vec_count(&s->members);
            for (int32_t mi = 0; mi < n; ++mi) {
                ecs_member_t* m = ecs_vec_get_t(&s->members, ecs_member_t, mi);
                const EcsPrimitive* p = ecs_get(w, m->type, EcsPrimitive);
                if (p == nullptr) {
                    continue;
                }

                Field f;
                if (!primitive_info(p->kind, f.size, f.real)) {
                    continue;
                }
                f.name = m->name != nullptr ? m->name : "";
                f.kind = kind_from_primitive(p->kind);
                f.offset = static_cast<uint32_t>(m->offset);
                f.count = (m->count != 0) ? static_cast<uint32_t>(m->count) : 1;
                if (f.real && quantum > 0) {
                    f.quantum = quantum;
                    f.bytes = bytes;
                }

                c.wire += static_cast<uint16_t>(f.count * (f.quantum > 0 ? f.bytes : f.size));
                c.fields.push_back(f);
            }
        } else {
            c.tag = true;
        }

        ids[c.name] = id;
        components.push_back(std::move(c));
        ++id;
    }
}

void NetworkRegistry::write(flecs::world& world, flecs::entity e, const Component& c, serialize::Writer& out) {
    if (c.tag) {
        return;
    }

    const void* base = ecs_get_id(world.c_ptr(), e.id(), c.entity);
    if (base == nullptr) {
        return;
    }
    const auto* p = static_cast<const uint8_t*>(base);

    for (const auto& f : c.fields) {
        for (uint32_t k = 0; k < f.count; ++k) {
            const uint8_t* elem = p + f.offset + (static_cast<size_t>(k * f.size));
            if (f.quantum > 0.0F) {
                float v;
                std::memcpy(&v, elem, sizeof(float));
                long q = std::lround(v / f.quantum);
                if (f.bytes == 2) {
                    out.put<int16_t>(static_cast<int16_t>(q));
                } else {
                    out.put<int32_t>(static_cast<int32_t>(q));
                }
            } else {
                out.bytes(elem, f.size);
            }
        }
    }
}

void NetworkRegistry::decode(const Component& c, void* dst, serialize::Reader& in) {
    auto* p = static_cast<uint8_t*>(dst);
    for (const auto& f : c.fields) {
        for (uint32_t k = 0; k < f.count; ++k) {
            uint8_t* elem = p + f.offset + (static_cast<size_t>(k * f.size));
            if (f.quantum > 0.0F) {
                long q = (f.bytes == 2) ? in.get<int16_t>() : in.get<int32_t>();
                float v = static_cast<float>(q) * f.quantum;
                std::memcpy(elem, &v, sizeof(float));
            } else {
                in.bytes(elem, f.size);
            }
        }
    }
}

void NetworkRegistry::read(flecs::world& world, flecs::entity e, const Component& c, serialize::Reader& in) {
    if (c.tag) {
        ecs_add_id(world.c_ptr(), e.id(), c.entity);
        return;
    }
    void* base = ecs_ensure_id(world.c_ptr(), e.id(), c.entity, c.size);
    decode(c, base, in);
    ecs_modified_id(world.c_ptr(), e.id(), c.entity);
}

auto NetworkRegistry::describe() const -> std::vector<MessageComponentDescriptor> {
    std::vector<MessageComponentDescriptor> out;
    out.reserve(components.size());
    for (const auto& c : components) {
        MessageComponentDescriptor d;
        d.id = c.id;
        d.name = c.name;
        d.wire_size = c.wire;
        d.tag = c.tag ? 1 : 0;
        d.fields.reserve(c.fields.size());
        for (const auto& f : c.fields) {
            d.fields.push_back({.name = f.name, .kind = static_cast<uint8_t>(f.kind), .count = static_cast<uint16_t>(f.count), .quantum = f.quantum, .bytes = f.bytes});
        }
        out.push_back(std::move(d));
    }
    return out;
}

void NetworkRegistry::adopt(flecs::world& world, const std::vector<MessageComponentDescriptor>& descs, std::unordered_map<uint16_t, uint16_t>& remap) {
    remap.clear();
    for (const auto& d : descs) {
        std::vector<Field> fields;
        fields.reserve(d.fields.size());
        for (const auto& fd : d.fields) {
            Field f;
            f.name = fd.name;
            f.kind = static_cast<FieldKind>(fd.kind);
            f.count = fd.count;
            f.quantum = fd.quantum;
            f.bytes = fd.bytes;
            kind_info(f.kind, f.size, f.real);
            fields.push_back(f);
        }

        auto it = ids.find(d.name);
        if (it != ids.end()) {
            Component& existing = components[it->second - 1];
            bool layout_differs = existing.fields.size() != fields.size();
            for (size_t fi = 0; !layout_differs && fi < fields.size(); ++fi) {
                const auto& a = existing.fields[fi];
                const auto& b = fields[fi];
                layout_differs = a.name != b.name || a.kind != b.kind || a.count != b.count || a.quantum != b.quantum || a.bytes != b.bytes;
            }
            if (existing.wire != d.wire_size || existing.tag != (d.tag != 0) || layout_differs) {
                if (!existing.tag && existing.entity != 0) {
                    world.entity(existing.entity).destruct();
                }
                existing.fields = std::move(fields);
                existing.wire = d.wire_size;
                existing.tag = d.tag != 0;
                if (existing.tag) {
                    existing.entity = world.entity(d.name.c_str()).id();
                    existing.size = 0;
                } else {
                    uint32_t size = 0;
                    existing.entity = register_runtime(world, d.name, existing.fields, size);
                    existing.size = size;
                }
            }
            remap[d.id] = it->second;
            continue;
        }

        Component c;
        c.name = d.name;
        c.wire = d.wire_size;
        c.tag = d.tag != 0;
        c.fields = std::move(fields);
        if (c.tag) {
            c.entity = world.entity(d.name.c_str()).id();
            c.size = 0;
        } else {
            uint32_t size = 0;
            c.entity = register_runtime(world, d.name, c.fields, size);
            c.size = size;
        }
        c.id = static_cast<uint16_t>(components.size() + 1);
        ids[d.name] = c.id;
        remap[d.id] = c.id;
        SDL_Log("registry: runtime-registered '%s' (%u fields, %u bytes)", d.name.c_str(), static_cast<unsigned>(c.fields.size()), static_cast<unsigned>(c.size));
        components.push_back(std::move(c));
    }
}

auto NetworkRegistry::register_runtime(flecs::world& world, const std::string& name, std::vector<Field>& fields, uint32_t& out_size) -> flecs::entity_t {
    ecs_world_t* w = world.c_ptr();

    ecs_entity_desc_t ed = {};
    ed.name = name.c_str();
    ecs_entity_t comp = ecs_entity_init(w, &ed);

    ecs_struct_desc_t sd = {};
    sd.entity = comp;
    constexpr size_t MAX_MEMBERS = 32;
    if (fields.size() > MAX_MEMBERS) {
        SDL_Log("registry: component '%s' has %zu fields; clamping to %zu (wire layout will mismatch)", name.c_str(), fields.size(), MAX_MEMBERS);
    }
    size_t field_count = fields.size() < MAX_MEMBERS ? fields.size() : MAX_MEMBERS;
    std::vector<std::string> mnames(field_count);
    for (size_t i = 0; i < field_count; ++i) {
        mnames[i] = !fields[i].name.empty() ? fields[i].name : ("f" + std::to_string(i));
        sd.members[i].name = mnames[i].c_str();
        sd.members[i].type = kind_primitive(fields[i].kind);
        sd.members[i].count = fields[i].count > 1 ? static_cast<int32_t>(fields[i].count) : 0;
    }
    ecs_struct_init(w, &sd);

    const ecs_type_info_t* ti = ecs_get_type_info(w, comp);
    out_size = (ti != nullptr) ? static_cast<uint32_t>(ti->size) : 0;
    if (const EcsStruct* s = ecs_get(w, comp, EcsStruct)) {
        int32_t n = ecs_vec_count(&s->members);
        for (int32_t mi = 0; mi < n && std::cmp_less(mi, fields.size()); ++mi) {
            ecs_member_t* m = ecs_vec_get_t(&s->members, ecs_member_t, mi);
            fields[mi].offset = static_cast<uint32_t>(m->offset);
        }
    }
    return comp;
}
