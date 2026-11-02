#include "render.hpp"

#include "block.hpp"
#include "frustum.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace {

constexpr int TILE_PX = 16;
constexpr int ATLAS_PX = ATLAS_TILES * TILE_PX;  // 128

struct Rgb {
    int r, g, b;
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

// Scales a color by a factor in percent, clamped to a byte.
Rgb shade(Rgb c, int percent) {
    auto f = [percent](int v) { return std::clamp(v * percent / 100, 0, 255); };
    return Rgb{f(c.r), f(c.g), f(c.b)};
}

// Base color varied by up to +-span percent of noise.
Rgb noisy(Rgb base, int x, int y, uint32_t salt, int span) {
    const int n = static_cast<int>(pixel_hash(x, y, salt) % static_cast<uint32_t>(2 * span + 1));
    return shade(base, 100 - span + n);
}

constexpr Rgb GRASS{96, 158, 56}, DIRT{128, 92, 62}, STONE{122, 124, 128}, SAND{214, 201, 152},
              BARK{98, 74, 46}, WOOD{180, 146, 94}, LEAF{56, 112, 44}, PLANK{166, 128, 78},
              DEEP{58, 56, 66};

// Color of pixel (x, y), both in [0, 16), of tile t. Original pixel art, made only from code.
Rgb tile_pixel(int t, int x, int y) {
    const uint32_t salt = static_cast<uint32_t>(t) + 1;
    switch (t) {
    case T_GRASS_TOP: return noisy(GRASS, x, y, salt, 12);
    case T_GRASS_SIDE: {
        // A grass fringe of uneven depth (3..5 px) over dirt.
        const int depth = 3 + static_cast<int>(pixel_hash(x, 0, salt) % 3);
        return y < depth ? noisy(GRASS, x, y, salt, 12) : noisy(DIRT, x, y, salt + 50, 10);
    }
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
    case T_LOG_TOP: {
        // Square growth rings around the center, with bark around the edge.
        const int ring = std::max(std::abs(2 * x - 15), std::abs(2 * y - 15)) / 2;  // 0..7
        if (ring == 7) return noisy(BARK, x, y, salt, 8);
        return noisy(ring % 2 == 0 ? WOOD : shade(WOOD, 84), x, y, salt, 5);
    }
    case T_LEAVES: {
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
    default: return Rgb{255, 0, 255};  // unused atlas cell
    }
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
            p[3] = 255;
        }
    return Image{px, ATLAS_PX, ATLAS_PX, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}

void Renderer::init() {
    const Image atlas = generate_atlas();
    const Texture2D tex = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    material_ = LoadMaterialDefault();
    material_.maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    ready_ = true;
}

void Renderer::upload(ChunkPos p, const MeshData& m) {
    remove(p);
    if (m.vertex_count() == 0) return;
    // UnloadMesh frees these with RL_FREE, so they must come from RL_MALLOC.
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
    mesh.indices = copy(m.indices);
    UploadMesh(&mesh, false);
    meshes_.emplace(p, mesh);
}

void Renderer::remove(ChunkPos p) {
    auto it = meshes_.find(p);
    if (it == meshes_.end()) return;
    UnloadMesh(it->second);
    meshes_.erase(it);
}

int Renderer::draw(const Camera3D& cam, float aspect) {
    // The same projection BeginMode3D builds, so culling matches what is on screen.
    const Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    const Matrix proj = MatrixPerspective(cam.fovy * DEG2RAD, aspect, rlGetCullDistanceNear(),
                                          rlGetCullDistanceFar());
    const Frustum f = frustum_from(MatrixMultiply(view, proj));
    int drawn = 0;
    for (const auto& [p, mesh] : meshes_) {
        const Vector3 lo{static_cast<float>(p.x * CHUNK), static_cast<float>(p.y * CHUNK),
                         static_cast<float>(p.z * CHUNK)};
        const Vector3 hi{lo.x + CHUNK, lo.y + CHUNK, lo.z + CHUNK};
        if (!aabb_visible(f, lo, hi)) continue;
        DrawMesh(mesh, material_, MatrixIdentity());
        ++drawn;
    }
    return drawn;
}

void Renderer::draw_highlight(int x, int y, int z) {
    DrawCubeWires(Vector3{x + 0.5f, y + 0.5f, z + 0.5f}, 1.002f, 1.002f, 1.002f, BLACK);
}

void Renderer::shutdown() {
    for (auto& entry : meshes_) UnloadMesh(entry.second);
    meshes_.clear();
    if (ready_) UnloadMaterial(material_);  // also unloads the atlas texture
    ready_ = false;
}
