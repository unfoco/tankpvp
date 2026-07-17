#include "render.h"

#include <cmath>

#include "component/input.h"
#include "component/network.h"
#include "component/world.h"
#include "util/ballistics.h"
#include "util/math.h"
#include "util/time.h"

namespace {

auto ease(float cur, float target, float rate, float dt) -> float {
    return cur + ((target - cur) * (1.0F - std::exp(-rate * dt)));
}

auto slot_for(FrameScratch& scratch, uint64_t texture, uint64_t normal_map, uint64_t shader, bool nearest) -> uint32_t {
    for (uint32_t i = 0; i < scratch.slots.size(); ++i) {
        const SlotInfo& s = scratch.slots[i];
        if (s.texture == texture && s.normal_map == normal_map && s.shader == shader && s.nearest == nearest) {
            return i;
        }
    }
    scratch.slots.push_back({.texture = texture, .normal_map = normal_map, .shader = shader, .nearest = nearest});
    return static_cast<uint32_t>(scratch.slots.size() - 1);
}

auto maskable(RenderPlane p) -> bool {
    return p.value >= plane::Entity.value && p.value < plane::Overhead.value;
}

template <class Src>
void fill_emitter(GpuEmitter& ge, const Src& src) {
    ge.speed = src.speed;
    ge.size = src.size;
    ge.life = src.life;
    ge.direction = src.direction;
    ge.spread = src.spread;
    ge.color_begin = src.color_begin;
    ge.color_end = src.color_end;
    ge.gravity = src.gravity;
    ge.drag = src.drag;
    ge.spin = src.spin;
    ge.grow = src.grow;
    ge.bounce = std::clamp(src.bounce, 0.0F, 1.0F);
    ge.emissive = src.emissive;
    uint32_t flags = src.collide ? particle_flags::COLLIDE : 0U;
    flags |= (static_cast<uint32_t>(src.blend) & 0xFU) << particle_flags::BLEND_SHIFT;
    ge.flags = flags;
}

}

void Render::camera(flecs::iter& it, size_t i, RenderState& render, const Position& pos) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(render.window, &width, &height);
    float base_zoom = std::min(static_cast<float>(width) / WIDTH, static_cast<float>(height) / HEIGHT);
    float dt = it.delta_time();
    if (dt <= 0.0F || dt > 0.1F) {
        dt = 1.0F / 60.0F;
    }

    flecs::entity self = it.entity(i);
    auto* cam = self.try_get_mut<Camera>();

    glm::vec2 focus = pos.value;
    if (const auto* prev = self.try_get<PrevPose>()) {
        if (const auto* fm = it.world().try_get<FrameMix>()) {
            glm::vec2 d = focus - prev->position;
            if (glm::dot(d, d) < 24.0F * 24.0F) {
                focus = glm::mix(prev->position, focus, std::clamp(fm->alpha, 0.0F, 1.0F));
            }
        }
    }
    float zoom = base_zoom;
    float rotation = 0.0F;
    glm::vec2 offset{0};
    float follow = 0.16F;
    if (cam != nullptr) {
        if (cam->focus.x != 0.0F || cam->focus.y != 0.0F) {
            focus = cam->focus;
        }
        zoom = base_zoom * (cam->zoom > 0.0F ? cam->zoom : 1.0F);
        rotation = cam->rotation;
        offset = cam->offset;
        follow = cam->follow > 0.0F ? cam->follow : 0.16F;
    }

    glm::vec2 delta = focus - render.camera.position;
    if (glm::dot(delta, delta) > 600.0F * 600.0F) {
        render.camera.position = focus;
    } else {
        render.camera.position += delta * (1.0F - std::exp(-(follow * 60.0F) * dt));
    }
    render.camera.zoom = ease(render.camera.zoom, zoom, 9.0F, dt);
    render.camera.rotation = ease(render.camera.rotation, rotation, 9.0F, dt);
    render.camera.offset.x = ease(render.camera.offset.x, offset.x, 12.0F, dt);
    render.camera.offset.y = ease(render.camera.offset.y, offset.y, 12.0F, dt);

    if (cam != nullptr && cam->shake > 0.0F) {
        float amp = cam->shake * cam->shake * 24.0F;
        render.shake_time += dt * 38.0F;
        render.camera.shakeOffset = {std::sin(render.shake_time * 1.3F) * amp, std::cos(render.shake_time * 1.9F) * amp};
        cam->shake = std::max(0.0F, cam->shake - (dt * 1.6F));
    } else {
        render.camera.shakeOffset = {0, 0};
    }
}

void Render::mesh_chunks(flecs::world world, RenderState& r) {
    const auto* tileset = world.try_get<Tileset>();
    if (tileset == nullptr) {
        return;
    }
    bool tileset_changed = r.tiles.tileset_version != tileset->version;
    r.tiles.tileset_version = tileset->version;

    auto build = [&](flecs::entity e, const TileChunk& chunk) -> void {
        std::vector<GpuTile> floor;
        std::vector<GpuTile> solid;
        floor.reserve(128);
        bool pending = false;
        for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                uint16_t id = chunk.tiles[(ly * CHUNK_SIZE) + lx];
                if (id == TILE_EMPTY) {
                    continue;
                }
                const TileType& type = tileset->type(id);
                uint32_t layer = atlas_layer(world, r, r.tile_atlas, type.texture, &pending);
                GpuTile tile = {};
                tile.position = {((static_cast<float>(chunk.cx) * CHUNK_SIZE) + static_cast<float>(lx) + 0.5F) * TILE_SIZE,
                                 ((static_cast<float>(chunk.cy) * CHUNK_SIZE) + static_cast<float>(ly) + 0.5F) * TILE_SIZE};
                tile.uv = {0, 0, 1, 1};
                tile.flags = layer | (type.solid ? 1U << 16 : 0U);
                if (type.solid) {
                    solid.push_back(tile);
                } else {
                    floor.push_back(tile);
                }
            }
        }
        auto& mesh = r.tiles.chunks[WorldGrid::key(chunk.cx, chunk.cy)];
        write_buffer(r, mesh.floor, floor.data(), floor.size() * sizeof(GpuTile), WGPUBufferUsage_Vertex);
        write_buffer(r, mesh.solid, solid.data(), solid.size() * sizeof(GpuTile), WGPUBufferUsage_Vertex);
        mesh.floor_count = static_cast<uint32_t>(floor.size());
        mesh.solid_count = static_cast<uint32_t>(solid.size());
        if (pending) {
            e.add<ChunkDirty>();
        } else {
            e.remove<ChunkDirty>();
        }
    };

    if (tileset_changed) {
        world.query_builder<const TileChunk>().build().each([&](flecs::entity e, const TileChunk& chunk) -> void { build(e, chunk); });
    } else {
        world.query_builder<const TileChunk>().with<ChunkDirty>().build().each([&](flecs::entity e, const TileChunk& chunk) -> void { build(e, chunk); });
    }

    const auto* grid = world.try_get<WorldGrid>();
    if (grid != nullptr) {
        for (auto it = r.tiles.chunks.begin(); it != r.tiles.chunks.end();) {
            if (!grid->data.contains(it->first)) {
                it = r.tiles.chunks.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void Render::collect(flecs::iter& it) {
    while (it.next()) {
        flecs::world world = it.world();
        auto* rp = world.try_get_mut<RenderState>();
        if (rp == nullptr || !rp->frame.ok) {
            return;
        }
        RenderState& r = *rp;
        FrameScratch& scratch = r.scratch;
        scratch.clear();
        r.params = {};

        float dt = it.delta_time();
        if (dt <= 0.0F || dt > 0.25F) {
            dt = 1.0F / 60.0F;
        }
        r.params.dt = dt;
        r.time += dt;

        auto& dg = r.diagnostics;
        ++dg.frames;
        double wall = util::now();
        if (dg.prev_wall > 0) {
            dg.max_dt = std::max(dg.max_dt, static_cast<float>(wall - dg.prev_wall));
        }
        dg.prev_wall = wall;
        if (dg.last_log == 0 || !world.has<NetworkDiagnose>()) {
            dg.last_log = wall;
        } else if (wall - dg.last_log >= 2.0) {
            float avg = dg.frames > 1 ? dg.step_sum / static_cast<float>(dg.frames - 1) : 0.0F;
            SDL_Log("rendergraph: fps=%.1f frame_max=%.1fms self_step_max=%.2f self_step_avg=%.2f",
                    static_cast<double>(dg.frames) / (wall - dg.last_log), dg.max_dt * 1000.0F, dg.step_max, avg);
            double keep = dg.prev_wall;
            dg = {};
            dg.last_log = wall;
            dg.prev_wall = keep;
        }

        const RenderQueries& q = r.queries;

        int ww = 0;
        int wh = 0;
        SDL_GetWindowSize(r.window, &ww, &wh);

        auto& by_nid = scratch.by_nid;
        q.nids.each([&](flecs::entity e, const NetworkId& id) -> void { by_nid[id.value] = e.id(); });

        float mix_alpha = 1.0F;
        if (const auto* fm = world.try_get<FrameMix>()) {
            mix_alpha = std::clamp(fm->alpha, 0.0F, 1.0F);
        }
        auto sample = [&](flecs::entity e, const Position* p, const Rotation* rr, glm::vec2& pos, float& rot) -> void {
            pos = p != nullptr ? p->value : glm::vec2{0};
            rot = rr != nullptr ? rr->angle : 0.0F;
            const auto* prev = e.try_get<PrevPose>();
            if (prev != nullptr && mix_alpha < 1.0F) {
                glm::vec2 d = pos - prev->position;
                if (glm::dot(d, d) < 24.0F * 24.0F) {
                    pos = glm::mix(prev->position, pos, mix_alpha);
                    rot = prev->angle + (math::angle_difference(rot, prev->angle) * mix_alpha);
                }
            }
        };

        auto transform_of = [&](flecs::entity e, glm::vec2& pos, float& rot, flecs::entity& out_parent) -> bool {
            const auto* p = e.try_get<Position>();
            const auto* rr = e.try_get<Rotation>();
            sample(e, p, rr, pos, rot);
            bool attached = false;
            if (const auto* attach = e.try_get<Attach>()) {
                flecs::entity parent;
                if (attach->parent != 0) {
                    auto pit = by_nid.find(attach->parent);
                    if (pit != by_nid.end()) {
                        parent = world.entity(pit->second);
                    }
                } else {
                    parent = e.parent();
                }
                if (parent && parent.is_alive()) {
                    out_parent = parent;
                    attached = true;
                    const auto* pp = parent.try_get<Position>();
                    const auto* pr = parent.try_get<Rotation>();
                    glm::vec2 base{0};
                    float prot = 0.0F;
                    sample(parent, pp, pr, base, prot);
                    float c = std::cos(prot);
                    float s = std::sin(prot);
                    pos = base + glm::vec2{(attach->offset.x * c) - (attach->offset.y * s), (attach->offset.x * s) + (attach->offset.y * c)};
                    rot = (attach->inherit_rotation ? prot : 0.0F) + attach->rotation + (rr != nullptr ? rr->angle : 0.0F);
                } else if (attach->parent != 0) {
                    return false;
                }
            }
            return p != nullptr || attached;
        };

        {
            Environment env{};
            q.environment.each([&](const Environment& e) -> void { env = e; });
            GpuTexture* tex = env.texture != 0 ? Render::texture(world, r, env.texture) : nullptr;
            GpuInstance bg = {};
            bg.tint = glm::vec4{tex != nullptr ? env.modulate : env.background, 1.0F};
            float size = env.texture_size;
            if (tex != nullptr && size <= 0.0F) {
                size = tex->size.x;
            }
            bg.size = {size, size};
            bg.flags = tex != nullptr ? 1U : 0U;
            bg.slot = slot_for(scratch, tex != nullptr ? env.texture : builtin::WHITE, 0, 0, false);
            scratch.instances.push_back(bg);
        }

        q.sprites.each([&](flecs::entity e, const Sprite& sprite) -> void {
            if (sprite.texture == 0 || e.has<Dying>() || e.has<Hidden>()) {
                return;
            }
            glm::vec2 pos{0};
            float rot = 0.0F;
            flecs::entity attach_parent;
            if (!transform_of(e, pos, rot, attach_parent)) {
                return;
            }
            if (const auto* trace = e.try_get<Trace>()) {
                pos += trace->offset;
            }
            if (e.has<Local>()) {
                auto& dg2 = r.diagnostics;
                if (dg2.has_prev) {
                    float step = glm::distance(pos, dg2.prev_self);
                    dg2.step_max = std::max(dg2.step_max, step);
                    dg2.step_sum += step;
                }
                dg2.prev_self = pos;
                dg2.has_prev = true;
            }
            if (attach_parent && (attach_parent.has<Dying>() || attach_parent.has<Hidden>())) {
                return;
            }
            GpuTexture* tex = Render::texture(world, r, sprite.texture);
            if (tex == nullptr) {
                return;
            }

            RenderDepth depth = e.has<RenderDepth>() ? e.get<RenderDepth>() : RenderDepth{};
            const auto* color = e.try_get<Color>();
            const auto* blend = e.try_get<Blend>();
            const auto* material = e.try_get<Material>();
            if (attach_parent) {
                if (color == nullptr) {
                    color = attach_parent.try_get<Color>();
                }
                if (blend == nullptr) {
                    blend = attach_parent.try_get<Blend>();
                }
            }

            glm::vec2 size = sprite.size;
            if (size.x <= 0.0F || size.y <= 0.0F) {
                size = {tex->size.x * std::abs(sprite.region.z - sprite.region.x) / SPRITE_TEXELS_PER_UNIT,
                        tex->size.y * std::abs(sprite.region.w - sprite.region.y) / SPRITE_TEXELS_PER_UNIT};
            }

            GpuInstance inst = {};
            inst.position = pos;
            inst.size = size;
            inst.pivot = sprite.pivot;
            inst.offset = sprite.offset;
            inst.uv = sprite.region;
            float opacity = blend != nullptr ? std::clamp(blend->opacity, 0.0F, 1.0F) : 1.0F;
            inst.tint = glm::vec4{color != nullptr ? color->value : glm::vec3{1.0F}, opacity};
            inst.rotation = rot;
            inst.flags = (sprite.flip_x ? instance_flags::FLIP_X : 0U) | (sprite.flip_y ? instance_flags::FLIP_Y : 0U);
            if (maskable(depth.plane)) {
                inst.flags |= instance_flags::MASKABLE;
            }
            uint64_t shader = 0;
            uint64_t normal_map = 0;
            if (material != nullptr) {
                shader = material->shader;
                normal_map = material->normal_map;
                inst.emissive = material->emissive;
                inst.dissolve = std::clamp(material->dissolve, 0.0F, 1.0F);
                inst.distortion = material->distortion;
                if (normal_map != 0) {
                    inst.flags |= instance_flags::NORMAL_MAP;
                }
                if (shader != 0) {
                    inst.param0 = {material->params[0], material->params[1], material->params[2], material->params[3]};
                    inst.param1 = {material->params[4], material->params[5], material->params[6], material->params[7]};
                } else {
                    inst.param0 = material->dissolve_edge;
                }
            }
            uint8_t blend_mode = blend != nullptr ? static_cast<uint8_t>(blend->mode) : 0;
            uint32_t slot = slot_for(scratch, sprite.texture, normal_map, shader, false);
            inst.slot = slot;

            uint8_t pipeline = static_cast<uint8_t>(blend_mode | (shader != 0 ? 8U : 0U));
            float y_bottom = pos.y + size.y * (1.0F - sprite.pivot.y) - r.camera.position.y;
            scratch.items.push_back({.key = sort_key(depth, y_bottom, pipeline, slot), .instance = static_cast<uint32_t>(scratch.instances.size())});
            scratch.instances.push_back(inst);
        });

        q.smoke.each([&](flecs::entity e, const Position& pos, const VisionBlocker& vb) -> void {
            GpuInstance inst = {};
            inst.position = pos.value;
            inst.size = {vb.radius * 2.0F, vb.radius * 2.0F};
            inst.pivot = {0.5F, 0.5F};
            inst.uv = {0, 0, 1, 1};
            inst.tint = glm::vec4{vb.color, std::clamp(vb.strength, 0.0F, 1.0F) * 0.92F};
            uint32_t slot = slot_for(scratch, builtin::DISC, 0, 0, false);
            inst.slot = slot;
            RenderDepth depth{.plane = plane::Entity + 900, .y_sort = false};
            scratch.items.push_back({.key = sort_key(depth, 0, 0, slot), .instance = static_cast<uint32_t>(scratch.instances.size())});
            scratch.instances.push_back(inst);

            GpuLight puff = {};
            puff.position = pos.value;
            puff.radius = vb.radius;
            puff.color = {vb.color, std::clamp(vb.strength, 0.0F, 1.0F)};
            puff.flags = static_cast<float>(light_flags::SMOKE);
            puff.falloff = 1.0F;
            scratch.smoke.push_back(puff);
        });

        std::sort(scratch.items.begin(), scratch.items.end(), [](const DrawItem& a, const DrawItem& b) -> bool { return a.key < b.key; });

        auto plane_of = [](SortKey key) -> int16_t { return static_cast<int16_t>(static_cast<int32_t>((key >> 45) & 0xFFFF) - 32768); };
        {
            std::vector<GpuInstance> ordered;
            ordered.reserve(scratch.instances.size());
            ordered.push_back(scratch.instances[0]);
            std::vector<DrawRun> rebuilt;
            DrawRun pending = {};
            bool pending_open = false;
            for (const DrawItem& item : scratch.items) {
                const GpuInstance& inst = scratch.instances[item.instance];
                const SlotInfo& info = scratch.slots[inst.slot];
                uint8_t blend = static_cast<uint8_t>((item.key >> 20) & 0x7);
                int16_t pl = plane_of(item.key);
                bool same = pending_open && pending.texture == info.texture && pending.normal_map == info.normal_map && pending.shader == info.shader &&
                            pending.blend == blend && pending.plane == pl;
                if (!same) {
                    if (pending_open) {
                        rebuilt.push_back(pending);
                    }
                    pending = {.first = static_cast<uint32_t>(ordered.size()), .count = 0, .texture = info.texture, .normal_map = info.normal_map,
                               .shader = info.shader, .blend = blend, .nearest = info.nearest, .plane = pl};
                    pending_open = true;
                }
                ordered.push_back(inst);
                ++pending.count;
            }
            if (pending_open) {
                rebuilt.push_back(pending);
            }
            scratch.instances = std::move(ordered);
            scratch.runs = std::move(rebuilt);
        }
        r.params.runs_floor = scratch.runs.size();
        r.params.runs_entities = scratch.runs.size();
        r.params.runs_overhead = scratch.runs.size();
        for (size_t i = 0; i < scratch.runs.size(); ++i) {
            if (scratch.runs[i].plane >= plane::Floor.value) {
                r.params.runs_floor = i;
                break;
            }
        }
        for (size_t i = r.params.runs_floor; i < scratch.runs.size(); ++i) {
            if (scratch.runs[i].plane >= plane::Entity.value) {
                r.params.runs_entities = i;
                break;
            }
        }
        for (size_t i = r.params.runs_entities; i < scratch.runs.size(); ++i) {
            if (scratch.runs[i].plane >= plane::Overhead.value) {
                r.params.runs_overhead = i;
                break;
            }
        }
        r.params.runs_entities = std::max(r.params.runs_entities, r.params.runs_floor);
        r.params.runs_overhead = std::max(r.params.runs_overhead, r.params.runs_entities);

        q.occluders.each([&](flecs::entity e, const Position& pos, const Occluder& occ) -> void {
            if (scratch.occluders.size() >= MAX_OCCLUDERS) {
                return;
            }
            GpuOccluder go = {};
            go.position = pos.value;
            go.half = occ.half;
            if (const auto* rot = e.try_get<Rotation>()) {
                go.rotation = rot->angle;
            }
            go.opacity = std::clamp(occ.opacity, 0.0F, 1.0F);
            scratch.occluders.push_back(go);
        });

        const auto* grid = world.try_get<WorldGrid>();
        const auto* tileset = world.try_get<Tileset>();
        auto build_shadows = [&](glm::vec2 L, float R) -> ShadowedLight {
            ShadowedLight sl{};
            sl.vert_first = static_cast<uint32_t>(scratch.shadow_verts.size());
            const float far = R * 2.0F;
            auto cast = [&](glm::vec2 a, glm::vec2 b, glm::vec2 normal) -> void {
                glm::vec2 mid = (a + b) * 0.5F;
                if (glm::dot(normal, L - mid) <= 0.0F) {
                    return;
                }
                glm::vec2 da = a - L;
                glm::vec2 db = b - L;
                float la = glm::length(da);
                float lb = glm::length(db);
                if (la < 0.001F || lb < 0.001F) {
                    return;
                }
                glm::vec2 a2 = a + (da / la) * far;
                glm::vec2 b2 = b + (db / lb) * far;
                scratch.shadow_verts.push_back(a);
                scratch.shadow_verts.push_back(b);
                scratch.shadow_verts.push_back(b2);
                scratch.shadow_verts.push_back(a);
                scratch.shadow_verts.push_back(b2);
                scratch.shadow_verts.push_back(a2);
            };

            if (grid != nullptr && tileset != nullptr) {
                int tx0 = static_cast<int>(std::floor((L.x - R) / TILE_SIZE));
                int tx1 = static_cast<int>(std::floor((L.x + R) / TILE_SIZE));
                int ty0 = static_cast<int>(std::floor((L.y - R) / TILE_SIZE));
                int ty1 = static_cast<int>(std::floor((L.y + R) / TILE_SIZE));
                constexpr int CAP = 200000;
                auto solid = [&](int tx, int ty) -> bool {
                    return ballistics::solid(ballistics::tile_at(*grid, *tileset, (static_cast<float>(tx) + 0.5F) * TILE_SIZE, (static_cast<float>(ty) + 0.5F) * TILE_SIZE));
                };
                for (int ty = ty0; ty <= ty1 && static_cast<int>(scratch.shadow_verts.size()) < CAP; ++ty) {
                    for (int tx = tx0; tx <= tx1; ++tx) {
                        if (!solid(tx, ty)) {
                            continue;
                        }
                        float x0 = static_cast<float>(tx) * TILE_SIZE;
                        float y0 = static_cast<float>(ty) * TILE_SIZE;
                        float x1 = x0 + TILE_SIZE;
                        float y1 = y0 + TILE_SIZE;
                        if (!solid(tx, ty - 1)) {
                            cast({x0, y0}, {x1, y0}, {0.0F, -1.0F});
                        }
                        if (!solid(tx, ty + 1)) {
                            cast({x1, y1}, {x0, y1}, {0.0F, 1.0F});
                        }
                        if (!solid(tx - 1, ty)) {
                            cast({x0, y1}, {x0, y0}, {-1.0F, 0.0F});
                        }
                        if (!solid(tx + 1, ty)) {
                            cast({x1, y0}, {x1, y1}, {1.0F, 0.0F});
                        }
                    }
                }
            }

            for (const GpuOccluder& go : scratch.occluders) {
                glm::vec2 d = go.position - L;
                if (std::abs(d.x) > R + go.half.x || std::abs(d.y) > R + go.half.y) {
                    continue;
                }
                float c = std::cos(go.rotation);
                float s = std::sin(go.rotation);
                auto corner = [&](float sx, float sy) -> glm::vec2 {
                    glm::vec2 l = {sx * go.half.x, sy * go.half.y};
                    return go.position + glm::vec2{(l.x * c) - (l.y * s), (l.x * s) + (l.y * c)};
                };
                glm::vec2 tl = corner(-1, -1);
                glm::vec2 tr = corner(1, -1);
                glm::vec2 br = corner(1, 1);
                glm::vec2 bl = corner(-1, 1);
                glm::vec2 nx{c, s};
                glm::vec2 ny{-s, c};
                cast(tl, tr, -ny);
                cast(br, bl, ny);
                cast(bl, tl, -nx);
                cast(tr, br, nx);
            }

            sl.vert_count = static_cast<uint32_t>(scratch.shadow_verts.size()) - sl.vert_first;
            return sl;
        };

        uint32_t shadow_count = 0;
        q.lights.each([&](flecs::entity e, const Position& pos, const Light& light) -> void {
            GpuLight gl = {};
            gl.position = pos.value;
            gl.radius = light.radius;
            gl.softness = std::clamp(light.softness, 0.0F, 1.0F);
            float flicker = 1.0F;
            if (light.flicker > 0.0F) {
                float phase = static_cast<float>(r.time) * 13.0F + static_cast<float>(e.id() % 97);
                flicker = 1.0F - light.flicker * (0.5F + 0.5F * std::sin(phase * 1.7F) * std::sin(phase * 0.93F));
            }
            gl.color = {light.color, std::max(light.intensity, 0.0F) * flicker};
            uint32_t flags = 0;
            if (light.cone > 0.0F) {
                flags |= light_flags::CONE;
                gl.cone = light.cone;
                if (const auto* rot = e.try_get<Rotation>()) {
                    gl.direction = rot->angle;
                }
            }
            gl.falloff = 1.6F;
            bool shadows = light.shadows && shadow_count < r.quality.max_shadow_lights;
            if (shadows) {
                ++shadow_count;
                gl.flags = static_cast<float>(flags | light_flags::SHADOWS);
                ShadowedLight sl = build_shadows(pos.value, light.radius);
                sl.light = gl;
                scratch.shadowed.push_back(sl);
            } else if (scratch.lights.size() < r.quality.max_lights) {
                gl.flags = static_cast<float>(flags);
                scratch.lights.push_back(gl);
            }
        });

        glm::vec4 ambient{1.0F, 1.0F, 1.0F, 1.0F};
        q.local_view.each([&](flecs::entity self, const Position& pos) -> void {
            if (const auto* vision = self.try_get<Vision>()) {
                ambient = {vision->ambient_color, std::clamp(vision->ambient, 0.0F, 1.0F)};
                if (vision->kind != VisionKind::None) {
                    glm::vec2 origin = pos.value;
                    float facing = self.try_get<Rotation>() ? self.get<Rotation>().angle : 0.0F;
                    if (const auto* cam = self.try_get<Camera>(); cam != nullptr && cam->target != 0) {

                        origin = cam->focus;
                        flecs::entity t = world.entity(cam->target);
                        if (t.is_alive()) {
                            if (const auto* tr = t.try_get<Rotation>()) {
                                facing = tr->angle;
                            }
                        }
                    }
                    r.params.vision = true;
                    r.params.vision_solid = vision->solid;
                    GpuLight gl = {};
                    gl.position = origin;
                    gl.radius = vision->range;
                    gl.softness = 0.25F;
                    gl.color = {0.0F, 0.0F, 0.0F, 1.0F};
                    uint32_t flags = light_flags::VISION | light_flags::SHADOWS;
                    if (vision->kind == VisionKind::Cone) {
                        flags |= light_flags::CONE;
                        gl.cone = vision->angle;
                        gl.direction = facing;
                    }
                    gl.flags = static_cast<float>(flags);
                    gl.falloff = 0.45F;
                    ShadowedLight sl = build_shadows(origin, vision->range);
                    sl.light = gl;
                    scratch.shadowed.push_back(sl);
                }
            }
        });

        uint32_t emitter_index = 0;
        q.emitters.each([&](flecs::entity e, const Position& pos, ParticleEmitter& em, EmitterState& state) -> void {
            if (scratch.emitters.size() >= MAX_EMITTERS) {
                return;
            }
            state.accumulator += em.rate * dt;
            auto spawn = static_cast<uint32_t>(state.accumulator);
            state.accumulator -= static_cast<float>(spawn);
            spawn += state.pending_burst;
            state.pending_burst = 0;
            if (spawn == 0) {
                return;
            }
            GpuEmitter ge = {};
            fill_emitter(ge, em);
            ge.origin = pos.value;
            ge.spawn_half = em.spawn_half;
            if (em.local_space) {
                ge.flags |= particle_flags::LOCAL_SPACE;
            }
            ge.texture_slot = Render::atlas_layer(world, r, r.particle_atlas, em.texture != 0 ? em.texture : builtin::DISC);
            ge.spawn_count = std::min(spawn, 4096U);
            ge.seed = static_cast<uint32_t>((r.frame_index * 2654435761ULL) ^ (e.id() * 97ULL));
            ge.emitter_index = emitter_index++;
            scratch.emitters.push_back(ge);
        });
        for (GpuEmitter& burst : r.pending_bursts) {
            if (scratch.emitters.size() >= MAX_EMITTERS) {
                break;
            }
            burst.emitter_index = emitter_index++;
            burst.seed ^= static_cast<uint32_t>(r.frame_index * 7919ULL);
            scratch.emitters.push_back(burst);
        }
        r.pending_bursts.clear();

        Render::mesh_chunks(world, r);

        if (auto* handle = world.try_get_mut<MinimapHandle>(); handle != nullptr && handle->size > 0.0F) {
            r.params.minimap_active = true;
            r.params.minimap_range = handle->range > 0.0F ? handle->range : 1700.0F;
            glm::vec2 center = r.camera.position;
            q.local_view.each([&](flecs::entity, const Position& p) -> void { center = p.value; });
            r.params.minimap_center = center;
            Render::resize_minimap(r, static_cast<uint32_t>(handle->size * r.dpi));
        }

        const Targets& t = r.targets;
        glm::vec2 internal_px{static_cast<float>(t.scene.width), static_cast<float>(t.scene.height)};
        float pixel_zoom = r.camera.zoom * (internal_px.x / std::max(static_cast<float>(ww), 1.0F));
        glm::vec2 center = r.camera.position - (r.camera.offset / std::max(r.camera.zoom, 0.0001F));
        glm::vec2 shake = r.camera.shakeOffset / std::max(r.camera.zoom, 0.0001F);

        {
            float mx = 0.0F;
            float my = 0.0F;
            SDL_GetMouseState(&mx, &my);
            float z = std::max(r.camera.zoom, 0.0001F);
            glm::vec2 rel = (glm::vec2{mx, my} - (glm::vec2{static_cast<float>(ww), static_cast<float>(wh)} * 0.5F)) / z;
            float cr = std::cos(r.camera.rotation);
            float sr = std::sin(r.camera.rotation);
            glm::vec2 world_rel = {(rel.x * cr) - (rel.y * sr), (rel.x * sr) + (rel.y * cr)};
            world.set<Pointer>({.world = center + world_rel, .valid = true});
        }

        GpuCamera frame = Render::camera_uniform(r, center, internal_px, pixel_zoom, r.camera.rotation, shake);
        r.queue->writeBuffer(*r.frame_ubo.buffer, 0, &frame, sizeof(frame));

        glm::vec2 occluder_px{static_cast<float>(t.occluder.width), static_cast<float>(t.occluder.height)};
        GpuCamera occluder_view = Render::camera_uniform(r, center, occluder_px, pixel_zoom, r.camera.rotation, shake);
        r.queue->writeBuffer(*r.occluder_ubo.buffer, 0, &occluder_view, sizeof(occluder_view));

        if (r.params.minimap_active) {
            glm::vec2 mm_px{static_cast<float>(t.minimap.width), static_cast<float>(t.minimap.height)};
            float mm_zoom = mm_px.x / std::max(r.params.minimap_range, 1.0F);
            GpuCamera mm = Render::camera_uniform(r, r.params.minimap_center, mm_px, mm_zoom, 0.0F, {0, 0});
            r.queue->writeBuffer(*r.minimap_ubo.buffer, 0, &mm, sizeof(mm));
        }

        r.params.composite.ambient = ambient;
        r.params.composite.bloom = r.quality.bloom ? 0.35F : 0.0F;
        r.params.composite.exposure = 1.0F;
        r.params.composite.visibility = r.params.vision ? 1.0F : 0.0F;
        r.queue->writeBuffer(*r.composite_ubo.buffer, 0, &r.params.composite, sizeof(GpuComposite));

        PostStack post{};
        q.local_view.each([&](flecs::entity self, const Position&) -> void {
            if (const auto* p = self.try_get<PostStack>()) {
                post = *p;
            }
        });
        GpuPost gp = {};
        gp.tint = post.tint;
        gp.flash = post.flash;
        gp.vignette = post.vignette;
        gp.chromatic = post.chromatic;
        gp.pixelate = post.pixelate;
        gp.crt = post.crt;
        gp.dither = post.dither;
        gp.saturation = post.saturation;
        gp.screen = {static_cast<float>(t.ldr_a.width), static_cast<float>(t.ldr_a.height)};
        gp.time = static_cast<float>(r.time);
        gp.distortion = post.distortion;
        r.params.post = gp;
        r.params.blur = post.blur;
        r.queue->writeBuffer(*r.post_ubo.buffer, 0, &gp, sizeof(gp));

        auto upload = [&](GpuBuffer& buf, const auto& vec) -> void {
            Render::write_buffer(r, buf, vec.data(), vec.size() * sizeof(vec[0]), WGPUBufferUsage_Vertex);
        };
        upload(r.sprite_instances, scratch.instances);

        scratch.light_upload.assign(scratch.lights.begin(), scratch.lights.end());
        scratch.light_upload.insert(scratch.light_upload.end(), scratch.smoke.begin(), scratch.smoke.end());
        upload(r.light_instances, scratch.light_upload);

        scratch.shadow_upload.clear();
        for (const ShadowedLight& sl : scratch.shadowed) {
            scratch.shadow_upload.push_back(sl.light);
        }
        upload(r.shadow_light_instances, scratch.shadow_upload);

        upload(r.shadow_verts, scratch.shadow_verts);
        upload(r.occluder_instances, scratch.occluders);
        Render::particles_upload(r, dt);
    }
}

void Render::emit(flecs::entity e, const RequestParticles& req) {
    flecs::world world = e.world();
    auto* r = world.try_get_mut<RenderState>();
    if (r == nullptr) {
        return;
    }
    GpuEmitter ge = {};
    fill_emitter(ge, req);
    ge.origin = req.position;
    ge.spawn_half = {4.0F, 4.0F};
    ge.texture_slot = Render::atlas_layer(world, *r, r->particle_atlas, req.texture != 0 ? req.texture : builtin::DISC);
    ge.spawn_count = std::min<uint32_t>(req.count, 4096U);
    ge.seed = static_cast<uint32_t>(e.id());
    r->pending_bursts.push_back(ge);
}
