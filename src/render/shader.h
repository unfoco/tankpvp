#pragma once


namespace shader {


inline constexpr const char* FRAME = R"(
struct Frame {
    center: vec2f,
    extent: vec2f,
    screen: vec2f,
    shake: vec2f,
    zoom: f32,
    rotation: f32,
    time: f32,
    dpi: f32,
}
@group(0) @binding(0) var<uniform> F: Frame;
@group(0) @binding(1) var noise_tex: texture_2d<f32>;
@group(0) @binding(2) var frame_samp: sampler;

fn rot2(v: vec2f, a: f32) -> vec2f {
    let c = cos(a); let s = sin(a);
    return vec2f(v.x * c - v.y * s, v.x * s + v.y * c);
}
fn world_to_clip(world: vec2f) -> vec4f {
    let d = rot2(world - F.center - F.shake, F.rotation);
    let ndc = d / (F.extent * 0.5);
    return vec4f(ndc.x, -ndc.y, 0.0, 1.0);
}
fn clip01_to_world(uv: vec2f) -> vec2f {
    let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let d = rot2(ndc * F.extent * 0.5, -F.rotation);
    return d + F.center + F.shake;
}
fn noise2(p: vec2f) -> f32 {
    return textureSampleLevel(noise_tex, frame_samp, p, 0.0).r;
}
const QUAD = array<vec2f, 6>(
    vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0),
    vec2f(0.0, 0.0), vec2f(1.0, 1.0), vec2f(0.0, 1.0));

struct SceneOut {
    @location(0) scene: vec4f,
    @location(1) aux: vec4f,
}
)";


inline constexpr const char* SPRITE_COMMON = R"(
@group(1) @binding(0) var albedo_tex: texture_2d<f32>;
@group(1) @binding(1) var albedo_samp: sampler;
@group(1) @binding(2) var normal_tex: texture_2d<f32>;

struct In {
    @location(0) pos_size: vec4f,
    @location(1) pivot_offset: vec4f,
    @location(2) uv: vec4f,
    @location(3) tint: vec4f,
    @location(4) extra: vec4f,
    @location(5) flags: vec4u,
    @location(6) param0: vec4f,
    @location(7) param1: vec4f,
}
struct VO {
    @builtin(position) clip: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
    @location(2) world: vec2f,
    @location(3) extra: vec4f,
    @location(4) @interpolate(flat) flags: u32,
    @location(5) param0: vec4f,
    @location(6) param1: vec4f,
}

@vertex
fn vs_main(in: In, @builtin(vertex_index) vi: u32) -> VO {
    let corner = QUAD[vi % 6u];
    let size = in.pos_size.zw;
    let pivot = in.pivot_offset.xy;
    let local = (corner - pivot) * size + in.pivot_offset.zw;
    let world = in.pos_size.xy + rot2(local, in.extra.x);

    var uv = mix(in.uv.xy, in.uv.zw, corner);
    if ((in.flags.x & 1u) != 0u) { uv.x = in.uv.x + in.uv.z - uv.x; }
    if ((in.flags.x & 2u) != 0u) { uv.y = in.uv.y + in.uv.w - uv.y; }

    var out: VO;
    out.clip = world_to_clip(world);
    out.uv = uv;
    out.tint = in.tint;
    out.world = world;
    out.extra = in.extra;
    out.flags = in.flags.x;
    out.param0 = in.param0;
    out.param1 = in.param1;
    return out;
}

struct MaterialIn {
    uv: vec2f,
    world: vec2f,
    screen: vec2f,
    tint: vec4f,
    time: f32,
    param0: vec4f,
    param1: vec4f,
}
struct MaterialOut {
    color: vec4f,
    normal: vec2f,
    emissive: f32,
    distortion: vec2f,
}
)";

inline constexpr const char* SPRITE_BUILTIN_MATERIAL = R"(
fn material(in: MaterialIn) -> MaterialOut {
    var out: MaterialOut;
    out.color = textureSample(albedo_tex, albedo_samp, in.uv) * in.tint;
    out.normal = vec2f(0.0);
    out.emissive = 0.0;
    out.distortion = (in.uv - 0.5) * 2.0;
    return out;
}
)";

inline constexpr const char* SPRITE_FRAG = R"(

fn run_material(in: VO) -> MaterialOut {
    var min: MaterialIn;
    min.uv = in.uv;
    min.world = in.world;
    min.screen = in.clip.xy / F.screen;
    min.tint = in.tint;
    min.time = F.time;
    min.param0 = in.param0;
    min.param1 = in.param1;
    var m = material(min);
    if ((in.flags & 8u) != 0u) {
        m.normal = textureSample(normal_tex, albedo_samp, in.uv).rg * 2.0 - 1.0;
    }
    return m;
}

@fragment
fn fs_main(in: VO) -> SceneOut {
    var m = run_material(in);
    var color = m.color;

    let dissolve = in.extra.z;
    if (dissolve > 0.0) {
        let n = noise2(in.world / 96.0 + vec2f(in.extra.x * 0.13, 0.0));
        if (n < dissolve) { discard; }
        let edge = smoothstep(dissolve + 0.10, dissolve, n);
        color = vec4f(mix(color.rgb, in.param0.rgb * color.a, edge), color.a);
    }
    if (color.a <= 0.002) { discard; }

    var out: SceneOut;
    out.scene = vec4f(color.rgb * color.a, color.a);
    let emissive = clamp(in.extra.y + m.emissive, 0.0, 4.0) * 0.25;
    out.aux = vec4f(m.normal * 0.5 + 0.5, emissive, color.a);
    return out;
}

@fragment
fn fs_distort(in: VO) -> @location(0) vec2f {
    let m = run_material(in);
    return m.distortion * in.extra.w * m.color.a;
}

@fragment
fn fs_flat(in: VO) -> @location(0) vec4f {
    let m = run_material(in);
    if (m.color.a <= 0.002) { discard; }
    return vec4f(m.color.rgb * m.color.a, m.color.a);
}
)";

inline constexpr const char* SPRITE_FALLBACK_MATERIAL = R"(
fn material(in: MaterialIn) -> MaterialOut {
    let cell = floor(in.uv * 6.0 + F.time * 2.0);
    let k = abs(cell.x + cell.y) % 2.0;
    var out: MaterialOut;
    out.color = vec4f(mix(vec3f(1.0, 0.0, 0.9), vec3f(0.05, 0.05, 0.05), k), 1.0) * in.tint.a;
    out.normal = vec2f(0.0);
    out.emissive = 0.6 * (1.0 - k);
    out.distortion = vec2f(0.0);
    return out;
}
)";


inline constexpr const char* BACKGROUND = R"(
@group(1) @binding(0) var albedo_tex: texture_2d<f32>;
@group(1) @binding(1) var albedo_samp: sampler;
@group(1) @binding(2) var normal_tex: texture_2d<f32>;

struct In {
    @location(0) pos_size: vec4f,
    @location(1) pivot_offset: vec4f,
    @location(2) uv: vec4f,
    @location(3) tint: vec4f,
    @location(4) extra: vec4f,
    @location(5) flags: vec4u,
    @location(6) param0: vec4f,
    @location(7) param1: vec4f,
}
struct VO {
    @builtin(position) clip: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
    @location(2) @interpolate(flat) flags: u32,
    @location(3) @interpolate(flat) tile: vec2f,
}

@vertex
fn vs_main(in: In, @builtin(vertex_index) vi: u32) -> VO {
    let corner = QUAD[vi % 6u];
    var out: VO;
    out.clip = vec4f(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    out.uv = corner;
    out.tint = in.tint;
    out.flags = in.flags.x;
    out.tile = in.pos_size.zw;
    return out;
}


@fragment
fn fs_main(in: VO) -> SceneOut {
    var color = in.tint.rgb;
    if ((in.flags & 1u) != 0u) {
        let world = clip01_to_world(in.uv);
        let size = max(in.tile, vec2f(1.0, 1.0));
        color = textureSample(albedo_tex, albedo_samp, world / size).rgb * in.tint.rgb;
    }
    var out: SceneOut;
    out.scene = vec4f(color, 1.0);
    out.aux = vec4f(0.5, 0.5, 0.0, 1.0);
    return out;
}
)";


inline constexpr const char* TILE = R"(
@group(1) @binding(0) var atlas_tex: texture_2d_array<f32>;
@group(1) @binding(1) var atlas_samp: sampler;

const TILE_SIZE: f32 = 32.0;

struct In {
    @location(0) position: vec2f,
    @location(1) uv: vec4f,
    @location(2) flags: vec2u,
}
struct VO {
    @builtin(position) clip: vec4f,
    @location(0) uv: vec2f,
    @location(1) @interpolate(flat) layer: u32,
}

@vertex
fn vs_main(in: In, @builtin(vertex_index) vi: u32) -> VO {
    let corner = QUAD[vi % 6u];
    let world = in.position + (corner - 0.5) * TILE_SIZE;
    var out: VO;
    out.clip = world_to_clip(world);
    out.uv = corner;
    out.layer = in.flags.x & 0xFFFFu;
    return out;
}


@fragment
fn fs_main(in: VO) -> SceneOut {
    let c = textureSample(atlas_tex, atlas_samp, in.uv, in.layer);
    var out: SceneOut;
    out.scene = vec4f(c.rgb * c.a, c.a);
    out.aux = vec4f(0.5, 0.5, 0.0, c.a);
    return out;
}

@fragment
fn fs_mask(in: VO) -> @location(0) f32 {
    return 1.0;
}

@fragment
fn fs_silhouette(in: VO) -> @location(0) vec4f {
    let a = 0.5;
    return vec4f(vec3f(0.72) * a, a);
}
)";


inline constexpr const char* OCCLUDER = R"(
struct In {
    @location(0) pos_half: vec4f,
    @location(1) rot_opacity: vec4f,
}
struct VO {
    @builtin(position) clip: vec4f,
    @location(0) opacity: f32,
}

@vertex
fn vs_main(in: In, @builtin(vertex_index) vi: u32) -> VO {
    let corner = QUAD[vi % 6u];
    let local = (corner - 0.5) * in.pos_half.zw * 2.0;
    let world = in.pos_half.xy + rot2(local, in.rot_opacity.x);
    var out: VO;
    out.clip = world_to_clip(world);
    out.opacity = in.rot_opacity.y;
    return out;
}

@fragment
fn fs_main(in: VO) -> @location(0) f32 {
    return in.opacity;
}
)";


inline constexpr const char* LIGHT = R"(
@group(1) @binding(0) var occluder_tex: texture_2d<f32>;
@group(1) @binding(1) var aux_tex: texture_2d<f32>;
@group(1) @binding(2) var light_samp: sampler;
struct OView {
    center: vec2f,
    extent: vec2f,
    screen: vec2f,
    shake: vec2f,
    zoom: f32,
    rotation: f32,
    time: f32,
    dpi: f32,
}
@group(1) @binding(3) var<uniform> O: OView;

struct In {
    @location(0) pos_radius: vec4f,
    @location(1) color: vec4f,
    @location(2) extra: vec4f,
}
struct VO {
    @builtin(position) clip: vec4f,
    @location(0) world: vec2f,
    @location(1) @interpolate(flat) origin: vec2f,
    @location(2) @interpolate(flat) radius_soft: vec2f,
    @location(3) @interpolate(flat) color: vec4f,
    @location(4) @interpolate(flat) extra: vec4f,
}

@vertex
fn vs_main(in: In, @builtin(vertex_index) vi: u32) -> VO {
    let corner = QUAD[vi % 6u];
    let world = in.pos_radius.xy + (corner - 0.5) * in.pos_radius.z * 2.0;
    var out: VO;
    out.clip = world_to_clip(world);
    out.world = world;
    out.origin = in.pos_radius.xy;
    out.radius_soft = vec2f(in.pos_radius.z, in.pos_radius.w);
    out.color = in.color;
    out.extra = in.extra;
    return out;
}

fn light_value(in: VO) -> vec4f {
    let flags = u32(in.extra.z);
    let d = in.world - in.origin;
    let dist = length(d);
    let r = dist / max(in.radius_soft.x, 1.0);
    var atten = pow(clamp(1.0 - r, 0.0, 1.0), max(in.extra.w, 0.01));

    if ((flags & 2u) != 0u) {
        let ang = atan2(d.y, d.x);
        var diff = abs(ang - in.extra.y);
        diff = min(diff, 6.2831853 - diff);
        let half_angle = in.extra.x;
        let soft = max(in.radius_soft.y * 0.35 + 0.02, 0.02);
        atten *= 1.0 - smoothstep(half_angle - half_angle * soft, half_angle, diff);
    }
    if (atten <= 0.0005 || r >= 1.0) { return vec4f(0.0); }

    let suv = in.clip.xy / F.screen;
    let n2 = textureSampleLevel(aux_tex, light_samp, suv, 0.0).rg * 2.0 - 1.0;
    let n3 = normalize(vec3f(n2, max(1.0 - length(n2), 0.05)));
    let ldir = normalize(vec3f(in.origin - in.world, 64.0));
    let ndotl = clamp(dot(n3, ldir), 0.0, 1.0);

    if ((flags & 4u) != 0u) {
        return vec4f(in.color.rgb * in.color.a * atten * ndotl, atten);
    }
    return vec4f(in.color.rgb * in.color.a * atten * ndotl, atten * in.color.a);
}

@fragment
fn fs_light(in: VO) -> @location(0) vec4f {
    let v = light_value(in);
    if (v.a <= 0.0005 && dot(v.rgb, v.rgb) <= 0.000001) { discard; }
    return v;
}

@fragment
fn fs_light_solid(in: VO) -> @location(0) vec4f {
    return light_value(in);
}

@fragment
fn fs_smoke(in: VO) -> @location(0) vec4f {
    let d = length(in.world - in.origin) / max(in.radius_soft.x, 1.0);
    if (d >= 1.0) { discard; }
    let fall = (1.0 - d) * (1.0 - d);
    return vec4f(0.0, 0.0, 0.0, fall * in.color.a);
}
)";

inline constexpr const char* SHADOW_GEOM = R"(
struct VO {
    @builtin(position) clip: vec4f,
}

@vertex
fn vs_main(@location(0) position: vec2f) -> VO {
    var out: VO;
    out.clip = world_to_clip(position);
    return out;
}

@fragment
fn fs_main(in: VO) -> @location(0) vec4f {
    return vec4f(0.0, 0.0, 0.0, 0.0);
}
)";


inline constexpr const char* FULLSCREEN_VS = R"(
struct FsVO {
    @builtin(position) clip: vec4f,
    @location(0) uv: vec2f,
}
@vertex
fn vs_fullscreen(@builtin(vertex_index) vi: u32) -> FsVO {
    let corner = QUAD[vi % 6u];
    var out: FsVO;
    out.clip = vec4f(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    out.uv = corner;
    return out;
}
)";

inline constexpr const char* COMPOSITE = R"(
@group(1) @binding(0) var scene_tex: texture_2d<f32>;
@group(1) @binding(1) var aux_tex: texture_2d<f32>;
@group(1) @binding(2) var light_tex: texture_2d<f32>;
@group(1) @binding(3) var bloom_tex: texture_2d<f32>;
@group(1) @binding(4) var comp_samp: sampler;
struct Composite {
    ambient: vec4f,
    bloom: f32,
    pad0: f32,
    exposure: f32,
    visibility: f32,
}
@group(1) @binding(5) var<uniform> C: Composite;
@group(1) @binding(6) var overhead_tex: texture_2d<f32>;

@fragment
fn fs_lit(in: FsVO) -> @location(0) vec4f {
    let scene = textureSampleLevel(scene_tex, comp_samp, in.uv, 0.0);
    let ent = textureSampleLevel(bloom_tex, comp_samp, in.uv, 0.0);
    let over = textureSampleLevel(overhead_tex, comp_samp, in.uv, 0.0);
    let aux = textureSampleLevel(aux_tex, comp_samp, in.uv, 0.0);
    let light = textureSampleLevel(light_tex, comp_samp, in.uv, 0.0);
    let l = C.ambient.rgb * C.ambient.a + light.rgb + vec3f(aux.b * 4.0);
    let vis = mix(1.0, clamp(light.a, 0.0, 1.0), C.visibility);
    var rgb = scene.rgb * l * mix(1.0, mix(0.55, 1.0, vis), C.visibility);
    rgb = rgb * (1.0 - ent.a * vis) + ent.rgb * l * vis;
    rgb = rgb * (1.0 - over.a) + over.rgb * l;
    return vec4f(rgb, 1.0);
}

@fragment
fn fs_composite(in: FsVO) -> @location(0) vec4f {
    var hdr = textureSampleLevel(scene_tex, comp_samp, in.uv, 0.0).rgb;
    hdr += textureSampleLevel(bloom_tex, comp_samp, in.uv, 0.0).rgb * C.bloom;
    return vec4f(clamp(hdr * C.exposure, vec3f(0.0), vec3f(1.0)), 1.0);
}
)";

inline constexpr const char* BLOOM = R"(
@group(1) @binding(0) var src_tex: texture_2d<f32>;
@group(1) @binding(1) var src_samp: sampler;

@fragment
fn fs_down(in: FsVO) -> @location(0) vec4f {
    let texel = 1.0 / vec2f(textureDimensions(src_tex));
    var c = textureSampleLevel(src_tex, src_samp, in.uv, 0.0).rgb * 4.0;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(-1.0, -1.0), 0.0).rgb;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(1.0, -1.0), 0.0).rgb;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(-1.0, 1.0), 0.0).rgb;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(1.0, 1.0), 0.0).rgb;
    return vec4f(c / 8.0, 1.0);
}

@fragment
fn fs_threshold(in: FsVO) -> @location(0) vec4f {
    let texel = 1.0 / vec2f(textureDimensions(src_tex));
    var c = textureSampleLevel(src_tex, src_samp, in.uv, 0.0).rgb * 4.0;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(-1.0, -1.0), 0.0).rgb;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(1.0, -1.0), 0.0).rgb;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(-1.0, 1.0), 0.0).rgb;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(1.0, 1.0), 0.0).rgb;
    c /= 8.0;
    let bright = max(max(c.r, c.g), c.b);
    let knee = clamp((bright - 1.0) / max(bright, 0.0001), 0.0, 1.0);
    return vec4f(c * knee, 1.0);
}

@fragment
fn fs_up(in: FsVO) -> @location(0) vec4f {
    let texel = 1.0 / vec2f(textureDimensions(src_tex));
    var c = textureSampleLevel(src_tex, src_samp, in.uv, 0.0).rgb * 2.0;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(-1.0, 0.0), 0.0).rgb;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(1.0, 0.0), 0.0).rgb;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(0.0, -1.0), 0.0).rgb;
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(0.0, 1.0), 0.0).rgb;
    return vec4f(c / 6.0, 1.0);
}

@fragment
fn fs_blur(in: FsVO) -> @location(0) vec4f {
    let texel = 1.5 / vec2f(textureDimensions(src_tex));
    var c = textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(-1.0, -1.0), 0.0);
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(1.0, -1.0), 0.0);
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(-1.0, 1.0), 0.0);
    c += textureSampleLevel(src_tex, src_samp, in.uv + texel * vec2f(1.0, 1.0), 0.0);
    return c * 0.25;
}
)";


inline constexpr const char* POST = R"(
@group(1) @binding(0) var src_tex: texture_2d<f32>;
@group(1) @binding(1) var distort_tex: texture_2d<f32>;
@group(1) @binding(2) var post_samp: sampler;
struct Post {
    tint: vec4f,
    flash: f32,
    vignette: f32,
    pad0: f32,
    chromatic: f32,
    pixelate: f32,
    crt: f32,
    dither: f32,
    saturation: f32,
    screen: vec2f,
    time: f32,
    distortion: f32,
}
@group(1) @binding(3) var<uniform> P: Post;

const BAYER = array<f32, 16>(
    0.0, 8.0, 2.0, 10.0,
    12.0, 4.0, 14.0, 6.0,
    3.0, 11.0, 1.0, 9.0,
    15.0, 7.0, 13.0, 5.0);

@fragment
fn fs_main(in: FsVO) -> @location(0) vec4f {
    var uv = in.uv;

    if (P.crt > 0.001) {
        let c = uv * 2.0 - 1.0;
        let r2 = dot(c, c);
        let warped = c * (1.0 + P.crt * 0.12 * r2);
        uv = warped * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
            return vec4f(0.0, 0.0, 0.0, 1.0);
        }
    }
    if (P.pixelate > 1.0) {
        let cells = P.screen / P.pixelate;
        uv = (floor(uv * cells) + 0.5) / cells;
    }
    let off = textureSampleLevel(distort_tex, post_samp, uv, 0.0).rg * P.distortion * 0.05;
    uv += off;

    var color: vec3f;
    if (P.chromatic > 0.05) {
        let shift = vec2f(P.chromatic / P.screen.x, 0.0);
        color = vec3f(
            textureSampleLevel(src_tex, post_samp, uv - shift, 0.0).r,
            textureSampleLevel(src_tex, post_samp, uv, 0.0).g,
            textureSampleLevel(src_tex, post_samp, uv + shift, 0.0).b);
    } else {
        color = textureSampleLevel(src_tex, post_samp, uv, 0.0).rgb;
    }

    let gray = dot(color, vec3f(0.299, 0.587, 0.114));
    color = mix(vec3f(gray), color, P.saturation);

    if (P.crt > 0.001) {
        let line = 0.5 + 0.5 * sin(uv.y * P.screen.y * 3.14159);
        color *= mix(1.0, 0.72 + 0.28 * line, P.crt);
        let m = u32(in.clip.x) % 3u;
        var mask = vec3f(1.0);
        if (m == 0u) { mask = vec3f(1.0, 0.82, 0.82); }
        else if (m == 1u) { mask = vec3f(0.82, 1.0, 0.82); }
        else { mask = vec3f(0.82, 0.82, 1.0); }
        color *= mix(vec3f(1.0), mask, P.crt * 0.6);
    }
    if (P.dither > 0.001) {
        let ix = u32(in.clip.x) % 4u;
        let iy = u32(in.clip.y) % 4u;
        let threshold = (BAYER[iy * 4u + ix] + 0.5) / 16.0 - 0.5;
        let levels = mix(255.0, 10.0, clamp(P.dither, 0.0, 1.0));
        color = floor(color * levels + threshold * P.dither * 2.0 + 0.5) / levels;
    }
    if (P.vignette > 0.001) {
        let d = distance(in.uv, vec2f(0.5));
        color *= 1.0 - P.vignette * smoothstep(0.35, 0.78, d);
    }
    color = mix(color, P.tint.rgb, clamp(P.tint.a, 0.0, 1.0));
    color = mix(color, vec3f(1.0), clamp(P.flash, 0.0, 1.0));
    return vec4f(color, 1.0);
}
)";


inline constexpr const char* TRANSITION = R"(
@group(1) @binding(0) var world_tex: texture_2d<f32>;
@group(1) @binding(1) var ui_tex: texture_2d<f32>;
@group(1) @binding(2) var snap_tex: texture_2d<f32>;
@group(1) @binding(3) var tr_samp: sampler;
struct Transition {
    color: vec4f,
    center: vec2f,
    t: f32,
    kind: u32,
    direction: f32,
    aspect: f32,
    slide: u32,
    scope: u32,
}
@group(1) @binding(4) var<uniform> T: Transition;

fn ease(x: f32) -> f32 { return x * x * (3.0 - 2.0 * x); }
fn cl(uv: vec2f) -> vec2f { return clamp(uv, vec2f(0.0), vec2f(1.0)); }
fn world_at(uv: vec2f) -> vec4f { return textureSampleLevel(world_tex, tr_samp, cl(uv), 0.0); }
fn ui_at(uv: vec2f) -> vec4f { return textureSampleLevel(ui_tex, tr_samp, cl(uv), 0.0); }
fn snap_at(uv: vec2f) -> vec4f { return textureSampleLevel(snap_tex, tr_samp, cl(uv), 0.0); }
fn frame_at(uv: vec2f) -> vec4f { let w = world_at(uv); let u = ui_at(uv); return vec4f(w.rgb * (1.0 - u.a) + u.rgb, 1.0); }
fn inside01(uv: vec2f) -> f32 { return step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0); }

fn newc(uv: vec2f) -> vec4f { if (T.scope == 1u) { return ui_at(uv); } return frame_at(uv); }
fn oldc(uv: vec2f) -> vec4f { return snap_at(uv); }
fn finish(combined: vec4f, uv: vec2f) -> vec4f {
    if (T.scope == 1u) { let w = world_at(uv); return vec4f(w.rgb * (1.0 - combined.a) + combined.rgb, 1.0); }
    return combined;
}

@fragment
fn fs_main(in: FsVO) -> @location(0) vec4f {
    if (T.t >= 1.0) { return frame_at(in.uv); }
    let e = ease(clamp(T.t, 0.0, 1.0));

    if (T.kind == 1u) {
        var dir = vec2f(0.0);
        if (T.direction < 0.5) { dir = vec2f(-1.0, 0.0); }
        else if (T.direction < 1.5) { dir = vec2f(1.0, 0.0); }
        else if (T.direction < 2.5) { dir = vec2f(0.0, -1.0); }
        else { dir = vec2f(0.0, 1.0); }
        if (T.slide == 2u) {
            let suv = in.uv + dir * e;
            let combined = mix(newc(in.uv), oldc(suv), inside01(suv));
            return finish(combined, in.uv);
        }
        let cuv = in.uv - dir * (1.0 - e);
        var suv = in.uv + dir * e;
        if (T.slide == 1u) { suv = in.uv; }
        let combined = mix(oldc(suv), newc(cuv), inside01(cuv));
        return finish(combined, in.uv);
    }
    if (T.kind == 2u) {
        let n = noise2(in.uv * vec2f(T.aspect, 1.0) * 6.0);
        let m = smoothstep(e - 0.08, e + 0.08, n);
        var col = finish(mix(newc(in.uv), oldc(in.uv), m), in.uv);
        if (T.scope == 0u) {
            let edge = smoothstep(e - 0.02, e, n) * (1.0 - smoothstep(e, e + 0.02, n));
            col = col + T.color * edge * step(0.001, e) * step(e, 0.999);
        }
        return col;
    }
    if (T.kind == 3u) {
        let bell = 1.0 - abs(e * 2.0 - 1.0);
        let cells = max(F.screen / max(bell * 64.0, 1.0), vec2f(1.0));
        let puv = (floor(in.uv * cells) + 0.5) / cells;
        return finish(mix(oldc(puv), newc(puv), e), in.uv);
    }
    if (T.kind == 4u) {
        let d = distance((in.uv - T.center) * vec2f(T.aspect, 1.0), vec2f(0.0));
        let r = e * (T.aspect + 1.0);
        let m = smoothstep(r - 0.02, r, d);
        return finish(mix(newc(in.uv), oldc(in.uv), m), in.uv);
    }
    var col = finish(mix(oldc(in.uv), newc(in.uv), e), in.uv);
    if (T.scope == 0u) {
        let dip = (1.0 - abs(e * 2.0 - 1.0)) * T.color.a;
        col = mix(col, vec4f(T.color.rgb, 1.0), dip);
    }
    return col;
}
)";

inline constexpr const char* FULLSCREEN_STANDALONE = R"(
const QUAD = array<vec2f, 6>(
    vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0),
    vec2f(0.0, 0.0), vec2f(1.0, 1.0), vec2f(0.0, 1.0));

struct FsVO {
    @builtin(position) clip: vec4f,
    @location(0) uv: vec2f,
}
@vertex
fn vs_fullscreen(@builtin(vertex_index) vi: u32) -> FsVO {
    let corner = QUAD[vi % 6u];
    var out: FsVO;
    out.clip = vec4f(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    out.uv = corner;
    return out;
}
)";

inline constexpr const char* COMPOSE = R"(
@group(0) @binding(0) var world_tex: texture_2d<f32>;
@group(0) @binding(1) var ui_tex: texture_2d<f32>;
@group(0) @binding(2) var cmp_samp: sampler;

@fragment
fn fs_main(in: FsVO) -> @location(0) vec4f {
    let w = textureSampleLevel(world_tex, cmp_samp, in.uv, 0.0);
    let u = textureSampleLevel(ui_tex, cmp_samp, in.uv, 0.0);
    return vec4f(w.rgb * (1.0 - u.a) + u.rgb, 1.0);
}
)";

inline constexpr const char* BLIT = R"(
@group(0) @binding(0) var src_tex: texture_2d<f32>;
@group(0) @binding(1) var src_samp: sampler;

@fragment
fn fs_main(in: FsVO) -> @location(0) vec4f {
    return textureSampleLevel(src_tex, src_samp, in.uv, 0.0);
}
)";


inline constexpr const char* PARTICLES_COMPUTE = R"(
struct Particle {
    pos: vec2f,
    vel: vec2f,
    color_begin: vec4f,
    color_end: vec4f,
    life: f32,
    max_life: f32,
    size: f32,
    grow: f32,
    rotation: f32,
    spin: f32,
    drag: f32,
    gravity: f32,
    bounce: f32,
    emissive: f32,
    flags: u32,
    texture_slot: u32,
}
struct Emitter {
    origin: vec2f,
    spawn_half: vec2f,
    speed: vec2f,
    size: vec2f,
    life: vec2f,
    direction: f32,
    spread: f32,
    color_begin: vec4f,
    color_end: vec4f,
    gravity: f32,
    drag: f32,
    spin: f32,
    grow: f32,
    bounce: f32,
    emissive: f32,
    flags: u32,
    texture_slot: u32,
    spawn_count: u32,
    seed: u32,
    emitter_index: u32,
    pad: u32,
}
struct Indirect {
    vertex_count: u32,
    instance_count: atomic<u32>,
    first_vertex: u32,
    first_instance: u32,
}
struct Counters {
    spawn_total: atomic<u32>,
    dt_bits: u32,
    capacity: u32,
}
struct OView {
    center: vec2f,
    extent: vec2f,
    screen: vec2f,
    shake: vec2f,
    zoom: f32,
    rotation: f32,
    time: f32,
    dpi: f32,
}

@group(0) @binding(0) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(1) var<storage, read_write> indirect: Indirect;
@group(0) @binding(2) var<storage, read> emitters: array<Emitter>;
@group(0) @binding(3) var<storage, read_write> counters: Counters;
@group(0) @binding(4) var occluder_tex: texture_2d<f32>;
@group(0) @binding(5) var<uniform> O: OView;
@group(0) @binding(6) var<storage, read_write> alive: array<u32>;

fn pcg(v: u32) -> u32 {
    var state = v * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
fn rand01(seed: ptr<function, u32>) -> f32 {
    *seed = pcg(*seed);
    return f32(*seed) / 4294967295.0;
}
fn rot2c(v: vec2f, a: f32) -> vec2f {
    let c = cos(a); let s = sin(a);
    return vec2f(v.x * c - v.y * s, v.x * s + v.y * c);
}
fn mask_at(world: vec2f) -> f32 {
    let d = rot2c(world - O.center - O.shake, O.rotation);
    let uv = d / O.extent + 0.5;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x >= 1.0 || uv.y >= 1.0) { return 0.0; }
    let dims = vec2f(textureDimensions(occluder_tex));
    return textureLoad(occluder_tex, vec2i(uv * dims), 0).r;
}

@compute @workgroup_size(64)
fn cs_emit(@builtin(global_invocation_id) gid: vec3u) {
    let emitter_index = gid.x / 64u;
    let lane = gid.x % 64u;
    if (emitter_index >= arrayLength(&emitters)) { return; }
    let e = emitters[emitter_index];
    var i = lane;
    loop {
        if (i >= e.spawn_count) { break; }
        var seed = pcg(e.seed ^ (i * 2654435761u) ^ (emitter_index * 97u));
        let cursor = atomicAdd(&counters.spawn_total, 1u);
        let slot = cursor % counters.capacity;

        var p: Particle;
        p.pos = e.origin + (vec2f(rand01(&seed), rand01(&seed)) - 0.5) * 2.0 * e.spawn_half;
        let ang = e.direction + (rand01(&seed) - 0.5) * e.spread;
        let spd = mix(e.speed.x, e.speed.y, rand01(&seed));
        p.vel = vec2f(cos(ang), sin(ang)) * spd;
        p.color_begin = e.color_begin;
        p.color_end = e.color_end;
        p.max_life = max(mix(e.life.x, e.life.y, rand01(&seed)), 0.05);
        p.life = p.max_life;
        p.size = mix(e.size.x, e.size.y, rand01(&seed));
        p.grow = e.grow;
        p.rotation = rand01(&seed) * 6.2831853;
        p.spin = (rand01(&seed) - 0.5) * 2.0 * e.spin;
        p.drag = e.drag;
        p.gravity = e.gravity;
        p.bounce = e.bounce;
        p.emissive = e.emissive;
        p.flags = e.flags | (e.emitter_index << 16u);
        p.texture_slot = e.texture_slot;
        particles[slot] = p;
        i += 64u;
    }
}

@compute @workgroup_size(64)
fn cs_sim(@builtin(global_invocation_id) gid: vec3u) {
    let i = gid.x;
    if (i >= counters.capacity) { return; }
    var p = particles[i];
    if (p.life <= 0.0) { return; }
    let dt = bitcast<f32>(counters.dt_bits);

    p.vel.y += p.gravity * dt;
    p.vel *= max(0.0, 1.0 - p.drag * dt);
    var next = p.pos + p.vel * dt;

    if ((p.flags & 1u) != 0u && mask_at(next) > 0.5) {
        let nx = mask_at(p.pos + vec2f(sign(p.vel.x) * 2.0, 0.0));
        let ny = mask_at(p.pos + vec2f(0.0, sign(p.vel.y) * 2.0));
        if (nx > 0.5) { p.vel.x = -p.vel.x * p.bounce; }
        if (ny > 0.5) { p.vel.y = -p.vel.y * p.bounce; }
        if (nx <= 0.5 && ny <= 0.5) { p.vel = -p.vel * p.bounce; }
        if (dot(p.vel, p.vel) < 4.0) { p.vel = vec2f(0.0); }
        next = p.pos + p.vel * dt;
        if (mask_at(next) > 0.5) { next = p.pos; }
    }
    p.pos = next;
    p.rotation += p.spin * dt;
    p.size = max(p.size + p.grow * dt, 0.5);
    p.life -= dt;
    particles[i] = p;

    if (p.life > 0.0) {
        let k = atomicAdd(&indirect.instance_count, 1u);
        alive[k] = i;
    }
}
)";

inline constexpr const char* PARTICLES_DRAW = R"(
struct Particle {
    pos: vec2f,
    vel: vec2f,
    color_begin: vec4f,
    color_end: vec4f,
    life: f32,
    max_life: f32,
    size: f32,
    grow: f32,
    rotation: f32,
    spin: f32,
    drag: f32,
    gravity: f32,
    bounce: f32,
    emissive: f32,
    flags: u32,
    texture_slot: u32,
}
@group(1) @binding(0) var atlas_tex: texture_2d_array<f32>;
@group(1) @binding(1) var atlas_samp: sampler;
@group(1) @binding(2) var<storage, read> particles: array<Particle>;
@group(1) @binding(3) var<storage, read> alive: array<u32>;

struct VO {
    @builtin(position) clip: vec4f,
    @location(0) uv: vec2f,
    @location(1) @interpolate(flat) color: vec4f,
    @location(2) @interpolate(flat) layer: u32,
    @location(3) @interpolate(flat) mode: u32,
    @location(4) @interpolate(flat) emissive: f32,
}

@vertex
fn vs_main(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VO {
    let p = particles[alive[ii]];
    let corner = QUAD[vi % 6u];
    let local = (corner - 0.5) * p.size;
    let world = p.pos + rot2(local, p.rotation);
    let t = 1.0 - clamp(p.life / p.max_life, 0.0, 1.0);

    var out: VO;
    out.clip = world_to_clip(world);
    out.uv = corner;
    out.color = mix(p.color_begin, p.color_end, t);
    out.layer = p.texture_slot;
    let blend = (p.flags >> 8u) & 0xFu;
    out.mode = select(0u, 1u, blend == 1u || blend == 4u);
    out.emissive = p.emissive;
    return out;
}


@fragment
fn fs_main(in: VO) -> SceneOut {
    let tex = textureSample(atlas_tex, atlas_samp, in.uv, in.layer);
    var color = tex * in.color;
    if (color.a <= 0.003) { discard; }
    var out: SceneOut;
    let a = select(color.a, 0.0, in.mode == 1u);
    out.scene = vec4f(color.rgb * color.a, a);
    out.aux = vec4f(0.5, 0.5, clamp(in.emissive, 0.0, 4.0) * 0.25, color.a);
    return out;
}

@fragment
fn fs_distort(in: VO) -> @location(0) vec2f {
    let tex = textureSample(atlas_tex, atlas_samp, in.uv, in.layer);
    return (in.uv - 0.5) * 2.0 * tex.a * in.color.a * 0.5;
}
)";

}
