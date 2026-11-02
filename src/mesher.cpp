#include "mesher.hpp"

#include "chunk.hpp"
#include "world.hpp"

#include <cstddef>

namespace {

constexpr int PAD = CHUNK + 2;

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

// Tile UVs per corner. Side faces: corners 1 and 2 (top) take v0, corners 0 and 1 take u1, so the
// tile reads upright and unmirrored from outside. Top and bottom faces use the same layout.
constexpr float CORNER_U[4] = {1, 1, 0, 0};
constexpr float CORNER_V[4] = {1, 0, 0, 1};

void emit_face(MeshData& m, const FaceSpec& f, Tile tile, float bx, float by, float bz) {
    const std::size_t base = m.vertex_count();
    const float size = 1.f / static_cast<float>(ATLAS_TILES);
    const float u0 = static_cast<float>(tile % ATLAS_TILES) * size;
    const float v0 = static_cast<float>(tile / ATLAS_TILES) * size;
    for (int c = 0; c < 4; ++c) {
        m.positions.push_back(bx + f.corners[c][0]);
        m.positions.push_back(by + f.corners[c][1]);
        m.positions.push_back(bz + f.corners[c][2]);
        m.texcoords.push_back(u0 + CORNER_U[c] * size);
        m.texcoords.push_back(v0 + CORNER_V[c] * size);
        m.normals.push_back(static_cast<float>(f.dx));
        m.normals.push_back(static_cast<float>(f.dy));
        m.normals.push_back(static_cast<float>(f.dz));
        m.colors.push_back(f.shade);
        m.colors.push_back(f.shade);
        m.colors.push_back(f.shade);
        m.colors.push_back(255);
    }
    // At most 16^3 / 2 blocks * 6 faces * 4 vertices = 49,152 < 65,536, so uint16_t holds every index.
    for (int k : {0, 1, 2, 0, 2, 3})
        m.indices.push_back(static_cast<uint16_t>(base + static_cast<std::size_t>(k)));
}

}  // namespace

BlockId Padded::at(int lx, int ly, int lz) const {
    return b[pad_index(lx, ly, lz)];
}

std::size_t MeshData::vertex_count() const {
    return positions.size() / 3;
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

MeshData build_mesh(const Padded& pad, ChunkPos p) {
    MeshData m;
    const int ox = p.x * CHUNK, oy = p.y * CHUNK, oz = p.z * CHUNK;
    for (int ly = 0; ly < CHUNK; ++ly)
        for (int lz = 0; lz < CHUNK; ++lz)
            for (int lx = 0; lx < CHUNK; ++lx) {
                const BlockDef& def = block_def(pad.at(lx, ly, lz));
                if (!def.opaque) continue;
                for (std::size_t f = 0; f < 6; ++f) {
                    const FaceSpec& spec = FACES[f];
                    if (block_def(pad.at(lx + spec.dx, ly + spec.dy, lz + spec.dz)).opaque) continue;
                    emit_face(m, spec, def.tiles[f], static_cast<float>(ox + lx),
                              static_cast<float>(oy + ly), static_cast<float>(oz + lz));
                }
            }
    return m;
}
