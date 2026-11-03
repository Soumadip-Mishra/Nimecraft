#include "mesher.hpp"

#include "chunk.hpp"
#include "world.hpp"

#include <algorithm>
#include <cstddef>

namespace {

constexpr int PAD = CHUNK + 2;
constexpr float LIQUID_TOP = 0.875f;  // top of a liquid with no same liquid above
constexpr float DIAG = 0.70710678f;   // 1 / sqrt(2), the cross-plant normal component

std::size_t pad_index(int lx, int ly, int lz) {
    return static_cast<std::size_t>((lx + 1) + (lz + 1) * PAD + (ly + 1) * PAD * PAD);
}

// -1, 0, or 1: which chunk along one axis a padded local coordinate falls in.
int side_of(int l) {
    return l < 0 ? -1 : (l >= CHUNK ? 1 : 0);
}

struct FaceSpec {
    int dx, dy, dz;          // neighbor direction = outward normal
    float corners[4][3];     // unit-cube corners, counter-clockwise seen from outside
    uint8_t shade;
};

// Indexed by Face (PX, NX, PY, NY, PZ, NZ). Side faces list bottom, top, top, bottom.
constexpr FaceSpec FACES[6] = {
    {1, 0, 0, {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, SHADE_X},
    {-1, 0, 0, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}}, SHADE_X},
    {0, 1, 0, {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, SHADE_TOP},
    {0, -1, 0, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, SHADE_BOTTOM},
    {0, 0, 1, {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, SHADE_Z},
    {0, 0, -1, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}, SHADE_Z},
};

struct CrossSpec {
    float nx, nz;
    float corners[4][3];  // bottom, top, top, bottom; counter-clockwise seen from the normal side
};

// Two diagonal planes, each listed for both of its sides (the back is the front reversed).
constexpr CrossSpec CROSS[4] = {
    {DIAG, -DIAG, {{0, 0, 0}, {0, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
    {-DIAG, DIAG, {{1, 0, 1}, {1, 1, 1}, {0, 1, 0}, {0, 0, 0}}},
    {-DIAG, -DIAG, {{0, 0, 1}, {0, 1, 1}, {1, 1, 0}, {1, 0, 0}}},
    {DIAG, DIAG, {{1, 0, 0}, {1, 1, 0}, {0, 1, 1}, {0, 0, 1}}},
};

// Tile UVs per corner. Side faces: corners 1 and 2 (top) take v0, corners 0 and 1 take u1, so the
// tile reads upright and unmirrored from outside. Top and bottom faces use the same layout.
constexpr float CORNER_U[4] = {1, 1, 0, 0};
constexpr float CORNER_V[4] = {1, 0, 0, 1};

struct Normal {
    float x, y, z;
};

// One quad at block (bx, by, bz). Corner y values are 0 or 1; the 1s are placed at `top`.
void emit_quad(MeshData& m, const float (&corners)[4][3], Normal n, uint8_t shade, Tile tile,
               float bx, float by, float bz, float top) {
    const std::size_t base = m.vertex_count();
    const float size = 1.f / static_cast<float>(ATLAS_TILES);
    const float u0 = static_cast<float>(tile % ATLAS_TILES) * size;
    const float v0 = static_cast<float>(tile / ATLAS_TILES) * size;
    for (int c = 0; c < 4; ++c) {
        m.positions.push_back(bx + corners[c][0]);
        m.positions.push_back(by + corners[c][1] * top);
        m.positions.push_back(bz + corners[c][2]);
        m.texcoords.push_back(u0 + CORNER_U[c] * size);
        m.texcoords.push_back(v0 + CORNER_V[c] * size);
        m.normals.push_back(n.x);
        m.normals.push_back(n.y);
        m.normals.push_back(n.z);
        m.colors.push_back(shade);
        m.colors.push_back(shade);
        m.colors.push_back(shade);
        m.colors.push_back(255);
    }
    for (int k : {0, 1, 2, 0, 2, 3})
        m.indices.push_back(static_cast<uint32_t>(base + static_cast<std::size_t>(k)));
}

MeshData& layer_mesh(ChunkMesh& cm, Layer layer) {
    switch (layer) {
    case Layer::Cutout: return cm.cutout;
    case Layer::Translucent: return cm.translucent;
    case Layer::Opaque: break;
    }
    return cm.opaque;
}

// Copies elements [first * per, (first + count) * per) of `in`.
template <typename T>
std::vector<T> slice(const std::vector<T>& in, std::size_t per, std::size_t first, std::size_t count) {
    const auto begin = in.begin() + static_cast<std::ptrdiff_t>(first * per);
    return std::vector<T>(begin, begin + static_cast<std::ptrdiff_t>(count * per));
}

}  // namespace

BlockId Padded::at(int lx, int ly, int lz) const {
    return b[pad_index(lx, ly, lz)];
}

std::size_t MeshData::vertex_count() const {
    return positions.size() / 3;
}

bool ChunkMesh::empty() const {
    return opaque.vertex_count() == 0 && cutout.vertex_count() == 0 && translucent.vertex_count() == 0;
}

Padded snapshot(const World& w, ChunkPos p) {
    // The 27 chunks around p, looked up once each; y outside the world is handled per block.
    const Chunk* around[3][3][3];
    for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                around[dy + 1][dz + 1][dx + 1] = w.chunk(ChunkPos{p.x + dx, p.y + dy, p.z + dz});

    Padded pad;
    for (int ly = -1; ly <= CHUNK; ++ly) {
        const int wy = p.y * CHUNK + ly;
        const int sy = side_of(ly);
        for (int lz = -1; lz <= CHUNK; ++lz) {
            const int sz = side_of(lz);
            for (int lx = -1; lx <= CHUNK; ++lx) {
                BlockId id = blk::AIR;
                if (wy < 0) {
                    id = blk::DEEPSTONE;
                } else if (wy < WORLD_HEIGHT) {
                    const int sx = side_of(lx);
                    const Chunk* c = around[sy + 1][sz + 1][sx + 1];
                    if (c != nullptr) id = c->get(lx - sx * CHUNK, ly - sy * CHUNK, lz - sz * CHUNK);
                }
                pad.b[pad_index(lx, ly, lz)] = id;
            }
        }
    }
    return pad;
}

ChunkMesh build_mesh(const Padded& pad, ChunkPos p) {
    ChunkMesh out;
    const int ox = p.x * CHUNK, oy = p.y * CHUNK, oz = p.z * CHUNK;
    for (int ly = 0; ly < CHUNK; ++ly)
        for (int lz = 0; lz < CHUNK; ++lz)
            for (int lx = 0; lx < CHUNK; ++lx) {
                const BlockId id = pad.at(lx, ly, lz);
                if (id == blk::AIR || id >= blk::COUNT) continue;
                const BlockDef& def = block_def(id);
                MeshData& m = layer_mesh(out, def.layer);
                const float bx = static_cast<float>(ox + lx), by = static_cast<float>(oy + ly),
                            bz = static_cast<float>(oz + lz);
                if (def.shape == Shape::Cross) {
                    for (const CrossSpec& q : CROSS)
                        emit_quad(m, q.corners, Normal{q.nx, 0.f, q.nz}, SHADE_X, def.tiles[PX], bx,
                                  by, bz, 1.f);
                    continue;
                }
                const bool translucent = def.layer == Layer::Translucent;
                const float top =
                    def.shape == Shape::Liquid && pad.at(lx, ly + 1, lz) != id ? LIQUID_TOP : 1.f;
                for (std::size_t f = 0; f < 6; ++f) {
                    const FaceSpec& spec = FACES[f];
                    const BlockId n = pad.at(lx + spec.dx, ly + spec.dy, lz + spec.dz);
                    if (block_def(n).opaque || (translucent && n == id)) continue;
                    const Normal normal{static_cast<float>(spec.dx), static_cast<float>(spec.dy),
                                        static_cast<float>(spec.dz)};
                    emit_quad(m, spec.corners, normal, spec.shade, def.tiles[f], bx, by, bz, top);
                }
            }
    return out;
}

std::vector<MeshData> split_mesh(const MeshData& m, std::size_t max_vertices) {
    std::vector<MeshData> parts;
    const std::size_t total = m.vertex_count();
    const std::size_t step = std::max<std::size_t>(4, max_vertices / 4 * 4);  // whole quads, >= 1
    for (std::size_t first = 0; first < total; first += step) {
        const std::size_t count = std::min(step, total - first);
        MeshData part;
        part.positions = slice(m.positions, 3, first, count);
        part.texcoords = slice(m.texcoords, 2, first, count);
        part.normals = slice(m.normals, 3, first, count);
        part.colors = slice(m.colors, 4, first, count);
        // Quad q owns vertices [4q, 4q + 4) and indices [6q, 6q + 6).
        part.indices = slice(m.indices, 6, first / 4, count / 4);
        for (uint32_t& i : part.indices) i = static_cast<uint32_t>(i - first);
        parts.push_back(std::move(part));
    }
    return parts;
}
