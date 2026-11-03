#include "render.hpp"

#include "block.hpp"
#include "frustum.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr int TILE_PX = 16;
constexpr int ATLAS_PX = ATLAS_TILES * TILE_PX;  // 128

constexpr float FOG_START_FRACTION = 0.7f;
constexpr float CUTOUT_ALPHA = 0.5f;  // Opaque and Cutout layers discard texels below this

// Sun and moon: discs SKY_DISTANCE from the eye (inside the default far plane of 1000).
constexpr float SKY_DISTANCE = 400.f, SUN_RADIUS = 26.f, MOON_RADIUS = 18.f;
constexpr int SKY_SEGMENTS = 24;
constexpr Color SUN_COLOR{255, 244, 196, 255}, SUN_HALO{255, 214, 140, 70}, MOON_COLOR{214, 222, 240, 255};

// Chunk shader, GLSL 330. Meshes are drawn with an identity model matrix, so vertexPosition is the
// world position the fog distance is measured from.
constexpr const char* CHUNK_VS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragWorld;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragWorld = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

constexpr const char* CHUNK_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragWorld;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 camPos;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform float alphaCutoff;
uniform float daylight;
out vec4 finalColor;
void main() {
    // fragColor: r sky light, g block light (both 0..1), b face shade * ambient occlusion.
    vec4 tex = texture(texture0, fragTexCoord) * colDiffuse;
    float light = max(fragColor.r * daylight, fragColor.g);
    vec4 c = vec4(tex.rgb * fragColor.b * (0.06 + 0.94 * pow(light, 1.6)), tex.a);
    if (c.a < alphaCutoff) discard;
    float d = length(fragWorld.xz - camPos.xz);
    float f = clamp((d - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    finalColor = vec4(mix(c.rgb, fogColor, f), c.a);
}
)";

struct Rgb {
    int r, g, b;
    int a = 255;
};

// Integer hash of a pixel and a salt, for deterministic texture noise.
uint32_t pixel_hash(int x, int y, uint32_t salt) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(y) * 0xD8163841u ^
                 salt * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

// Scales a color by a factor in percent, clamped to a byte. The result is opaque.
Rgb shade(Rgb c, int percent) {
    auto f = [percent](int v) { return std::clamp(v * percent / 100, 0, 255); };
    return Rgb{f(c.r), f(c.g), f(c.b)};
}

Rgb with_alpha(Rgb c, int a) {
    c.a = a;
    return c;
}

// Base color varied by up to +-span percent of noise.
Rgb noisy(Rgb base, int x, int y, uint32_t salt, int span) {
    const int n = static_cast<int>(pixel_hash(x, y, salt) % static_cast<uint32_t>(2 * span + 1));
    return shade(base, 100 - span + n);
}

constexpr Rgb GRASS{96, 158, 56}, DIRT{128, 92, 62}, STONE{122, 124, 128}, SAND{214, 201, 152},
              BARK{98, 74, 46}, WOOD{180, 146, 94}, LEAF{56, 112, 44}, PLANK{166, 128, 78},
              DEEP{58, 56, 66}, WATER{44, 92, 196}, GRAVEL{132, 126, 120}, CLAY{150, 158, 172},
              SNOW{236, 240, 246}, ICE{150, 192, 236}, SANDSTONE{206, 186, 128}, CACTUS{62, 126, 56},
              SPINE{220, 214, 170}, PINE_BARK{72, 52, 38}, PINE_WOOD{168, 136, 92},
              PINE_LEAF{36, 84, 56}, PETAL{208, 76, 118}, POLLEN{236, 198, 64}, QUARTZ{230, 224, 236},
              COAL{38, 38, 42}, COPPER{198, 112, 62}, TIN{190, 196, 204}, IRON{206, 164, 132},
              SILVER{232, 238, 246}, FLAME{238, 136, 40}, FLAME_CORE{255, 240, 170},
              IRON_FRAME{58, 58, 64}, GLOW{246, 186, 92}, STEM{214, 210, 190}, GLOWCAP_TEAL{64, 196, 180},
              SPOT{200, 255, 240};
constexpr Rgb CLEAR{0, 0, 0, 0};
constexpr int WATER_ALPHA = 170, ICE_ALPHA = 200;

// A grass or snow fringe of uneven depth (3..5 px) over dirt.
Rgb fringe(Rgb top, int span, int x, int y, uint32_t salt) {
    const int depth = 3 + static_cast<int>(pixel_hash(x, 0, salt) % 3);
    return y < depth ? noisy(top, x, y, salt, span) : noisy(DIRT, x, y, salt + 50, 10);
}

// Square growth rings around the center, with bark around the edge.
Rgb rings(Rgb wood, Rgb bark, int x, int y, uint32_t salt) {
    const int ring = std::max(std::abs(2 * x - 15), std::abs(2 * y - 15)) / 2;  // 0..7
    if (ring == 7) return noisy(bark, x, y, salt, 8);
    return noisy(ring % 2 == 0 ? wood : shade(wood, 84), x, y, salt, 5);
}

// Stone with 2x2-pixel clusters of an ore color.
Rgb ore(Rgb color, int x, int y, uint32_t salt) {
    const bool cluster = pixel_hash(x / 2, y / 2, salt + 11) % 6 == 0 && pixel_hash(x, y, salt + 13) % 4 != 0;
    if (cluster) return noisy(color, x, y, salt, 8);
    return noisy(STONE, x, y, salt, 6);
}

// Color of pixel (x, y), both in [0, 16), of tile t. Original pixel art, made only from code.
Rgb tile_pixel(int t, int x, int y) {
    const uint32_t salt = static_cast<uint32_t>(t) + 1;
    switch (t) {
    case T_GRASS_TOP: return noisy(GRASS, x, y, salt, 12);
    case T_GRASS_SIDE: return fringe(GRASS, 12, x, y, salt);
    case T_DIRT: return noisy(DIRT, x, y, salt, 12);
    case T_STONE: {
        const bool speck = pixel_hash(x, y, salt + 7) % 9 == 0;
        return noisy(speck ? shade(STONE, 78) : STONE, x, y, salt, 6);
    }
    case T_SAND: return noisy(SAND, x, y, salt, 5);
    case T_LOG_SIDE: {
        // Vertical bark grooves every 4 px, jittered per column.
        const bool groove = (x + static_cast<int>(pixel_hash(x, 0, salt) % 2)) % 4 == 0;
        return noisy(groove ? shade(BARK, 72) : BARK, x, y, salt, 8);
    }
    case T_LOG_TOP: return rings(WOOD, BARK, x, y, salt);
    case T_LEAVES: {
        if (pixel_hash(x, y, salt + 9) % 5 == 0) return CLEAR;  // gaps between the leaves
        const uint32_t h = pixel_hash(x, y, salt) % 7;
        return noisy(h == 0 ? shade(LEAF, 65) : (h == 1 ? shade(LEAF, 120) : LEAF), x, y, salt, 8);
    }
    case T_PLANKS: {
        // Four boards of 4 px; each board has its own seam position and tone.
        const int board = y / 4;
        const int seam = static_cast<int>(pixel_hash(board, 0, salt) % 16);
        if (y % 4 == 3 || x == seam) return shade(PLANK, 70);
        return noisy(shade(PLANK, 92 + board * 4), x, y, salt, 5);
    }
    case T_DEEPSTONE: {
        const bool vein = pixel_hash(x, y, salt + 3) % 11 == 0;
        return noisy(vein ? shade(DEEP, 140) : DEEP, x, y, salt, 8);
    }
    case T_WATER: {
        // Light ripple dashes on a blue body.
        const bool ripple = (x + y * 3 + static_cast<int>(pixel_hash(0, y, salt) % 4)) % 8 == 0;
        return with_alpha(noisy(ripple ? shade(WATER, 125) : WATER, x, y, salt, 4), WATER_ALPHA);
    }
    case T_GRAVEL: {
        // Pebbles of about 3 px, each with its own tone.
        const int tone = 78 + static_cast<int>(pixel_hash(x / 3, y / 3, salt) % 5) * 11;
        return noisy(shade(GRAVEL, tone), x, y, salt, 6);
    }
    case T_CLAY: return noisy(y % 5 == 0 ? shade(CLAY, 92) : CLAY, x, y, salt, 4);
    case T_SNOW: return noisy(SNOW, x, y, salt, 3);
    case T_SNOW_SIDE: return fringe(SNOW, 3, x, y, salt);
    case T_ICE: {
        const bool streak = (x - y + 16) % 7 == 0;
        return with_alpha(noisy(streak ? shade(ICE, 112) : ICE, x, y, salt, 3), ICE_ALPHA);
    }
    case T_SANDSTONE_TOP: {
        const bool rim = x == 0 || x == 15 || y == 0 || y == 15;
        return noisy(rim ? shade(SANDSTONE, 88) : SANDSTONE, x, y, salt, 4);
    }
    case T_SANDSTONE_SIDE: {
        // Horizontal bedding lines every 5 px under a lighter cap.
        if (y % 5 == 4) return noisy(shade(SANDSTONE, 82), x, y, salt, 3);
        return noisy(y < 3 ? shade(SANDSTONE, 108) : SANDSTONE, x, y, salt, 4);
    }
    case T_CACTUS_TOP: {
        const int ring = std::max(std::abs(2 * x - 15), std::abs(2 * y - 15)) / 2;  // 0..7
        if (ring == 0) return shade(CACTUS, 70);
        return noisy(ring >= 6 ? CACTUS : shade(CACTUS, 125), x, y, salt, 5);
    }
    case T_CACTUS_SIDE: {
        if (pixel_hash(x, y, salt + 5) % 23 == 0) return SPINE;
        return noisy(x % 4 == 1 ? shade(CACTUS, 80) : CACTUS, x, y, salt, 6);
    }
    case T_PINE_LOG_SIDE: {
        // Staggered bark plates.
        const bool groove = (x + (y / 3) * 2) % 5 == 0;
        return noisy(groove ? shade(PINE_BARK, 70) : PINE_BARK, x, y, salt, 8);
    }
    case T_PINE_LOG_TOP: return rings(PINE_WOOD, PINE_BARK, x, y, salt);
    case T_PINE_LEAVES: {
        if (pixel_hash(x, y, salt + 9) % 4 == 0) return CLEAR;
        return noisy((x + y) % 3 == 0 ? shade(PINE_LEAF, 75) : PINE_LEAF, x, y, salt, 8);
    }
    case T_TALL_GRASS: {
        // Blades of different heights rising from the bottom edge, gaps between them.
        const int top = 3 + static_cast<int>(pixel_hash(x, 0, salt + 1) % 9);
        const bool blade = pixel_hash(x, 1, salt) % 3 != 0;
        if (!blade || y < top) return CLEAR;
        return noisy(shade(GRASS, 90 + (15 - y) * 2), x, y, salt, 6);
    }
    case T_FLOWER: {
        // A diamond of petals around a pollen center on a short stem with two leaves.
        const int d = std::abs(2 * x - 15) + std::abs(2 * y - 9);
        if (d <= 4) return POLLEN;
        if (d <= 9) return noisy(PETAL, x, y, salt, 6);
        const bool stem = (x == 7 || x == 8) && y >= 8;
        const bool leaf = (y == 11 && (x == 9 || x == 10)) || (y == 12 && (x == 5 || x == 6));
        if (stem || leaf) return shade(GRASS, 80);
        return CLEAR;
    }
    case T_COAL_ORE: return ore(COAL, x, y, salt);
    case T_COPPER_ORE: return ore(COPPER, x, y, salt);
    case T_TIN_ORE: return ore(TIN, x, y, salt);
    case T_IRON_ORE: return ore(IRON, x, y, salt);
    case T_SILVER_ORE: return ore(SILVER, x, y, salt);
    case T_QUARTZ: {
        // Crossing facet lines.
        if ((x + y) % 6 == 0) return shade(QUARTZ, 82);
        return noisy((x - y + 16) % 6 == 0 ? shade(QUARTZ, 110) : QUARTZ, x, y, salt, 3);
    }
    case T_TORCH: {
        // A short wooden stick with a flame on top: white-yellow core, orange rim.
        if ((x == 7 || x == 8) && y >= 7) return noisy(WOOD, x, y, salt, 8);
        const int d = std::abs(2 * x - 15) + std::abs(2 * y - 9);  // around (7.5, 4.5)
        if (d <= 2) return FLAME_CORE;
        if (d <= 5 && y >= 2) return noisy(FLAME, x, y, salt, 6);
        return CLEAR;
    }
    case T_LANTERN: {
        // Dark iron frame and bars around warm glowing glass, with cut corners.
        if ((x <= 1 || x >= 14) && (y <= 1 || y >= 14)) return CLEAR;
        const bool frame = x <= 1 || x >= 14 || y <= 1 || y >= 14 || x == 7 || x == 8;
        if (frame) return noisy(IRON_FRAME, x, y, salt, 6);
        const int d = std::abs(2 * y - 15);
        return noisy(shade(GLOW, 112 - d * 2), x, y, salt, 4);
    }
    case T_GLOWCAP: {
        // A pale stem under a teal cap with bright spots.
        if ((x == 7 || x == 8) && y >= 9) return noisy(STEM, x, y, salt, 5);
        const int dx = 2 * x - 15, dy = 2 * y - 17;  // cap centred at (7.5, 8.5), drawn above y 9
        if (y <= 8 && dx * dx + 4 * dy * dy <= 144) {
            if (pixel_hash(x, y, salt + 3) % 7 == 0) return SPOT;
            return noisy(GLOWCAP_TEAL, x, y, salt, 8);
        }
        return CLEAR;
    }
    default: return Rgb{255, 0, 255};  // unused atlas cell
    }
}

// Copies one split_mesh part into a raylib Mesh and uploads it. UnloadMesh frees the CPU arrays
// with RL_FREE, so they come from RL_MALLOC.
Mesh to_gpu(const MeshData& m) {
    auto copy = [](const auto& v) {
        using T = typename std::decay_t<decltype(v)>::value_type;
        auto* out = static_cast<T*>(RL_MALLOC(v.size() * sizeof(T)));
        std::memcpy(out, v.data(), v.size() * sizeof(T));
        return out;
    };
    Mesh mesh{};
    mesh.vertexCount = static_cast<int>(m.vertex_count());
    mesh.triangleCount = static_cast<int>(m.indices.size() / 3);
    mesh.vertices = copy(m.positions);
    mesh.texcoords = copy(m.texcoords);
    mesh.normals = copy(m.normals);
    mesh.colors = copy(m.colors);
    // split_mesh keeps every part within MAX_PART_VERTICES, so each index fits 16 bits.
    mesh.indices = static_cast<unsigned short*>(RL_MALLOC(m.indices.size() * sizeof(unsigned short)));
    for (std::size_t i = 0; i < m.indices.size(); ++i)
        mesh.indices[i] = static_cast<unsigned short>(m.indices[i]);
    UploadMesh(&mesh, false);
    return mesh;
}

}  // namespace

Image generate_atlas() {
    auto* px = static_cast<unsigned char*>(RL_MALLOC(ATLAS_PX * ATLAS_PX * 4));
    for (int y = 0; y < ATLAS_PX; ++y)
        for (int x = 0; x < ATLAS_PX; ++x) {
            const int t = (y / TILE_PX) * ATLAS_TILES + x / TILE_PX;
            const Rgb c = t < T_COUNT ? tile_pixel(t, x % TILE_PX, y % TILE_PX) : Rgb{255, 0, 255};
            unsigned char* p = px + (static_cast<std::size_t>(y) * ATLAS_PX + x) * 4;
            p[0] = static_cast<unsigned char>(c.r);
            p[1] = static_cast<unsigned char>(c.g);
            p[2] = static_cast<unsigned char>(c.b);
            p[3] = static_cast<unsigned char>(c.a);
        }
    return Image{px, ATLAS_PX, ATLAS_PX, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}

std::vector<ChunkPos> sort_back_to_front(std::vector<ChunkPos> chunks, Vector3 eye) {
    // Squared distances in double: exact for chunk centres and float eyes far from the origin.
    auto dist2 = [&eye](ChunkPos p) {
        const double dx = static_cast<double>(p.x) * CHUNK + CHUNK / 2 - eye.x;
        const double dy = static_cast<double>(p.y) * CHUNK + CHUNK / 2 - eye.y;
        const double dz = static_cast<double>(p.z) * CHUNK + CHUNK / 2 - eye.z;
        return dx * dx + dy * dy + dz * dz;
    };
    std::sort(chunks.begin(), chunks.end(), [&dist2](ChunkPos a, ChunkPos b) {
        const double da = dist2(a), db = dist2(b);
        if (da != db) return da > db;
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    return chunks;
}

FogRange fog_range(int rd) {
    const float end = static_cast<float>(rd * CHUNK);
    return FogRange{FOG_START_FRACTION * end, end};
}

Vector3 sun_direction(float angle) {
    return Vector3{std::cos(angle), std::sin(angle), 0.f};
}

std::vector<Vector3> sky_disc(Vector3 eye, Vector3 dir, float distance, float radius, int segments) {
    segments = std::max(3, segments);
    const Vector3 c = Vector3Add(eye, Vector3Scale(dir, distance));
    // u and v span the disc plane; u x v = -dir, so increasing angle runs counter-clockwise as seen
    // from the eye.
    const Vector3 ref = std::abs(dir.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    const Vector3 u = Vector3Normalize(Vector3CrossProduct(dir, ref));
    const Vector3 v = Vector3CrossProduct(u, dir);
    auto rim = [&](int i) {
        const float a = 2.f * PI * static_cast<float>(i) / static_cast<float>(segments);
        return Vector3Add(c, Vector3Add(Vector3Scale(u, radius * std::cos(a)), Vector3Scale(v, radius * std::sin(a))));
    };
    std::vector<Vector3> out;
    out.reserve(static_cast<std::size_t>(segments) * 3);
    for (int i = 0; i < segments; ++i) {
        out.push_back(c);
        out.push_back(rim(i));
        out.push_back(rim(i + 1));
    }
    return out;
}

void Renderer::init() {
    const Image atlas = generate_atlas();
    const Texture2D tex = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    material_ = LoadMaterialDefault();
    material_.maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    // A shader that fails to compile comes back with the default shader's id (rlgl swaps it in),
    // which IsShaderValid accepts and UnloadShader skips, so test the id and free its locs here.
    const Shader shader = LoadShaderFromMemory(CHUNK_VS, CHUNK_FS);
    if (IsShaderValid(shader) && shader.id != rlGetShaderIdDefault()) {
        material_.shader = shader;
        loc_cam_ = GetShaderLocation(shader, "camPos");
        loc_fog_color_ = GetShaderLocation(shader, "fogColor");
        loc_fog_start_ = GetShaderLocation(shader, "fogStart");
        loc_fog_end_ = GetShaderLocation(shader, "fogEnd");
        loc_cutoff_ = GetShaderLocation(shader, "alphaCutoff");
        loc_daylight_ = GetShaderLocation(shader, "daylight");
    } else {
        MemFree(shader.locs);
        TraceLog(LOG_WARNING, "nimecraft: chunk shader failed; drawing without fog or cutout");
    }
    ready_ = true;
}

void Renderer::set_fog(FogRange range, Color sky) {
    fog_ = range;
    sky_ = sky;
}

void Renderer::set_daylight(float daylight) {
    daylight_ = daylight;
}

void Renderer::draw_sky(const Camera3D& cam, float sun_angle) {
    const Vector3 sun = sun_direction(sun_angle);
    const Vector3 moon{-sun.x, -sun.y, -sun.z};
    auto disc = [&cam](Vector3 dir, float radius, Color c) {
        if (dir.y < -0.25f) return;  // well below the horizon
        const std::vector<Vector3> tri = sky_disc(cam.position, dir, SKY_DISTANCE, radius, SKY_SEGMENTS);
        for (std::size_t i = 0; i + 2 < tri.size(); i += 3) DrawTriangle3D(tri[i], tri[i + 1], tri[i + 2], c);
    };
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    disc(sun, SUN_RADIUS * 1.6f, SUN_HALO);
    disc(sun, SUN_RADIUS, SUN_COLOR);
    disc(moon, MOON_RADIUS, MOON_COLOR);
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void Renderer::upload(ChunkPos p, const ChunkMesh& m) {
    remove(p);
    if (m.empty()) return;
    const MeshData* layers[3] = {&m.opaque, &m.cutout, &m.translucent};
    ChunkGpu gpu;
    for (std::size_t l = 0; l < gpu.size(); ++l)
        for (const MeshData& part : split_mesh(*layers[l])) gpu[l].push_back(to_gpu(part));
    meshes_.emplace(p, std::move(gpu));
}

void Renderer::remove(ChunkPos p) {
    auto it = meshes_.find(p);
    if (it == meshes_.end()) return;
    for (auto& layer : it->second)
        for (Mesh& mesh : layer) UnloadMesh(mesh);
    meshes_.erase(it);
}

int Renderer::draw(const Camera3D& cam, float aspect) {
    // The same projection BeginMode3D builds, so culling matches what is on screen.
    const Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    const Matrix proj = MatrixPerspective(cam.fovy * DEG2RAD, aspect, rlGetCullDistanceNear(),
                                          rlGetCullDistanceFar());
    const Frustum f = frustum_from(MatrixMultiply(view, proj));
    std::vector<const ChunkGpu*> visible;
    std::vector<ChunkPos> translucent;
    for (const auto& [p, gpu] : meshes_) {
        const Vector3 lo{static_cast<float>(p.x * CHUNK), static_cast<float>(p.y * CHUNK),
                         static_cast<float>(p.z * CHUNK)};
        const Vector3 hi{lo.x + CHUNK, lo.y + CHUNK, lo.z + CHUNK};
        if (!aabb_visible(f, lo, hi)) continue;
        visible.push_back(&gpu);
        if (!gpu[2].empty()) translucent.push_back(p);
    }

    const Shader& sh = material_.shader;
    auto set_float = [&sh](int loc, float v) {
        if (loc >= 0) SetShaderValue(sh, loc, &v, SHADER_UNIFORM_FLOAT);
    };
    if (loc_cam_ >= 0) SetShaderValue(sh, loc_cam_, &cam.position, SHADER_UNIFORM_VEC3);
    if (loc_fog_color_ >= 0) {
        const float sky[3] = {sky_.r / 255.f, sky_.g / 255.f, sky_.b / 255.f};
        SetShaderValue(sh, loc_fog_color_, sky, SHADER_UNIFORM_VEC3);
    }
    set_float(loc_fog_start_, fog_.start);
    set_float(loc_fog_end_, fog_.end);
    set_float(loc_daylight_, daylight_);

    // Opaque and Cutout: depth-tested and depth-writing, texels below CUTOUT_ALPHA discarded.
    set_float(loc_cutoff_, CUTOUT_ALPHA);
    for (std::size_t l = 0; l < 2; ++l)
        for (const ChunkGpu* gpu : visible)
            for (const Mesh& mesh : (*gpu)[l]) DrawMesh(mesh, material_, MatrixIdentity());

    // Translucent: back to front, alpha-blended, depth-tested but not depth-writing.
    set_float(loc_cutoff_, 0.0f);
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    for (ChunkPos p : sort_back_to_front(std::move(translucent), cam.position))
        for (const Mesh& mesh : meshes_.at(p)[2]) DrawMesh(mesh, material_, MatrixIdentity());
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    return static_cast<int>(visible.size());
}

void Renderer::draw_highlight(int x, int y, int z) {
    DrawCubeWires(Vector3{x + 0.5f, y + 0.5f, z + 0.5f}, 1.002f, 1.002f, 1.002f, BLACK);
}

void Renderer::shutdown() {
    for (auto& entry : meshes_)
        for (auto& layer : entry.second)
            for (Mesh& mesh : layer) UnloadMesh(mesh);
    meshes_.clear();
    if (ready_) UnloadMaterial(material_);  // also unloads the atlas texture
    ready_ = false;
}
