#include "terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

constexpr int MIN_HEIGHT = 48, MAX_HEIGHT = 103;
constexpr int SAND_MAX = SEA_LEVEL + 1;  // ground at or below this height is sand
constexpr int TREE_REACH = 2;            // leaves reach this far from a trunk
constexpr int TREE_TOP = 7;              // highest leaves sit at h + 7
static_assert(MAX_HEIGHT + TREE_TOP < WORLD_HEIGHT, "trees must fit in the column");

// Lattice value in [0,1]: the top 24 bits of the hash, exact in a float.
float lattice(uint32_t seed, int x, int z) {
    return static_cast<float>(hash2(seed, x, z) >> 8) / 16777215.0f;
}

float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

BlockId& cell(Column& col, int lx, int y, int lz) {
    return col[static_cast<std::size_t>(y / CHUNK)]->blocks[static_cast<std::size_t>(Chunk::index(lx, y % CHUNK, lz))];
}

struct Tree {
    int x, z, h;
};

}  // namespace

uint32_t hash2(uint32_t seed, int x, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed * 2246822519u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

float value_noise2(uint32_t seed, float x, float z) {
    const float fx = std::floor(x), fz = std::floor(z);
    const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
    const float sx = smoothstep(x - fx), sz = smoothstep(z - fz);
    const float a = lattice(seed, ix, iz), b = lattice(seed, ix + 1, iz);
    const float c = lattice(seed, ix, iz + 1), d = lattice(seed, ix + 1, iz + 1);
    const float near_row = a + (b - a) * sx;
    const float far_row = c + (d - c) * sx;
    // Rounding in the interpolation can step one ulp past an end of [0,1].
    return std::clamp(near_row + (far_row - near_row) * sz, 0.0f, 1.0f);
}

float fbm2(uint32_t seed, float x, float z, int octaves) {
    float sum = 0.0f, weights = 0.0f, amp = 1.0f, freq = 1.0f;
    uint32_t octave_seed = seed;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * value_noise2(octave_seed, x * freq, z * freq);
        weights += amp;
        amp *= 0.5f;
        freq *= 2.0f;
        octave_seed += 0x9E3779B9u;
    }
    return sum / weights;
}

int terrain_height(uint32_t seed, int x, int z) {
    const float n = fbm2(seed, static_cast<float>(x) / 96.0f, static_cast<float>(z) / 96.0f, 5);
    return std::clamp(MIN_HEIGHT + static_cast<int>(n * 56.0f), MIN_HEIGHT, MAX_HEIGHT);
}

bool tree_at(uint32_t seed, int x, int z) {
    return hash2(seed ^ 0x7EE5u, x, z) % 64 == 0 && terrain_height(seed, x, z) > SAND_MAX;
}

Column generate_column(uint32_t seed, ColumnPos pos) {
    Column col;
    for (int cy = 0; cy < SECTIONS; ++cy) {
        col[static_cast<std::size_t>(cy)] = std::make_unique<Chunk>();
        col[static_cast<std::size_t>(cy)]->pos = ChunkPos{pos.x, cy, pos.z};
    }
    const int x0 = pos.x * CHUNK, z0 = pos.z * CHUNK;

    for (int lz = 0; lz < CHUNK; ++lz) {
        for (int lx = 0; lx < CHUNK; ++lx) {
            const int h = terrain_height(seed, x0 + lx, z0 + lz);
            const bool sandy = h <= SAND_MAX;
            cell(col, lx, 0, lz) = blk::DEEPSTONE;
            for (int y = 1; y <= h - 4; ++y) cell(col, lx, y, lz) = blk::STONE;
            for (int y = h - 3; y < h; ++y) cell(col, lx, y, lz) = sandy ? blk::SAND : blk::DIRT;
            cell(col, lx, h, lz) = sandy ? blk::SAND : blk::GRASS;
        }
    }

    // Every origin whose leaves can reach this column; tx-major, tz-minor loops sort by (tx, tz).
    std::vector<Tree> trees;
    for (int tx = x0 - TREE_REACH; tx < x0 + CHUNK + TREE_REACH; ++tx)
        for (int tz = z0 - TREE_REACH; tz < z0 + CHUNK + TREE_REACH; ++tz)
            if (tree_at(seed, tx, tz)) trees.push_back(Tree{tx, tz, terrain_height(seed, tx, tz)});

    auto inside = [](int l) { return l >= 0 && l < CHUNK; };
    for (const Tree& t : trees) {
        const int lx = t.x - x0, lz = t.z - z0;
        if (!inside(lx) || !inside(lz)) continue;
        for (int y = t.h + 1; y <= t.h + 5; ++y) cell(col, lx, y, lz) = blk::LOG;
    }
    for (const Tree& t : trees) {
        for (int y = t.h + 4; y <= t.h + TREE_TOP; ++y) {
            const int r = y <= t.h + 5 ? TREE_REACH : 1;
            for (int dz = -r; dz <= r; ++dz) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (r == TREE_REACH && std::abs(dx) == r && std::abs(dz) == r) continue;  // corners
                    const int lx = t.x + dx - x0, lz = t.z + dz - z0;
                    if (!inside(lx) || !inside(lz)) continue;
                    BlockId& b = cell(col, lx, y, lz);
                    if (b == blk::AIR) b = blk::LEAVES;
                }
            }
        }
    }
    return col;
}
