#include "fluids.hpp"

#include "block.hpp"
#include "chunk.hpp"
#include "ticks.hpp"
#include "world.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdlib>

namespace {

// Horizontal directions: +X, -X, +Z, -Z; OPPOSITE[d] points back.
constexpr std::array<int, 4> DX = {1, -1, 0, 0}, DZ = {0, 0, 1, -1}, OPPOSITE = {1, 0, 3, 2};

bool loaded(const World& w, int x, int y, int z) {
    return y >= 0 && y < WORLD_HEIGHT && w.column_loaded(ColumnPos{floor_div(x, CHUNK), floor_div(z, CHUNK)});
}

bool is_water(const World& w, int x, int y, int z) {
    return w.get(x, y, z) == blk::WATER;
}

bool is_source(const World& w, int x, int y, int z) {
    return is_water(w, x, y, z) && w.meta(x, y, z) == WATER_SOURCE;
}

bool is_flowing(const World& w, int x, int y, int z) {
    return is_water(w, x, y, z) && w.meta(x, y, z) != WATER_SOURCE;
}

// The level water feeds with: a flowing level, or 0 for a source and for falling water (it lands
// at full strength).
int feed_level(uint8_t meta) {
    return meta >= 1 && meta <= WATER_MAX_LEVEL ? meta : 0;
}

// Water may move into the cell: loaded, and AIR or non-source WATER.
bool open_cell(const World& w, int x, int y, int z) {
    if (!loaded(w, x, y, z)) return false;
    const BlockId b = w.get(x, y, z);
    return b == blk::AIR || (b == blk::WATER && w.meta(x, y, z) != WATER_SOURCE);
}

// Water on the cell would fall: AIR or non-source WATER below (never at y = 0).
bool drop_below(const World& w, int x, int y, int z) {
    if (y == 0) return false;
    const BlockId b = w.get(x, y - 1, z);
    return b == blk::AIR || (b == blk::WATER && w.meta(x, y - 1, z) != WATER_SOURCE);
}

// The non-source water column starting at (x, y, z) still falls into AIR somewhere below.
bool draining(const World& w, int x, int y, int z) {
    while (y > 0 && is_flowing(w, x, y, z)) {
        const BlockId below = w.get(x, y - 1, z);
        if (below == blk::AIR) return true;
        --y;
    }
    return false;
}

// Water at q spreads to its sides only when it cannot fall: flowing water over any WATER drains
// into it, and a source waits until the water under it has landed.
bool spreads_sideways(const World& w, BlockPos q) {
    if (q.y == 0) return true;  // DEEPSTONE below
    const BlockId below = w.get(q.x, q.y - 1, q.z);
    if (below == blk::AIR) return false;
    if (below != blk::WATER) return true;
    if (w.meta(q.x, q.y, q.z) != WATER_SOURCE) return false;
    return !draining(w, q.x, q.y - 1, q.z);
}

// Steps from q to the nearest drop through open cells, starting with the cell beside q in
// direction d (step 1) and never crossing q; INT_MAX beyond FLUID_DROP_REACH.
int drop_distance(const World& w, BlockPos q, int d) {
    constexpr int R = FLUID_DROP_REACH, N = 2 * R + 1;
    struct Node {
        int ox, oz, dist;
    };
    std::array<bool, N * N> seen{};
    std::array<Node, N * N> queue{};
    const auto at = [](int ox, int oz) { return static_cast<std::size_t>((ox + R) + (oz + R) * N); };
    std::size_t head = 0, tail = 0;
    seen[at(0, 0)] = true;
    seen[at(DX[static_cast<std::size_t>(d)], DZ[static_cast<std::size_t>(d)])] = true;
    queue[tail++] = Node{DX[static_cast<std::size_t>(d)], DZ[static_cast<std::size_t>(d)], 1};
    while (head < tail) {
        const Node n = queue[head++];
        if (drop_below(w, q.x + n.ox, q.y, q.z + n.oz)) return n.dist;
        if (n.dist == R) continue;
        for (std::size_t e = 0; e < 4; ++e) {
            const int ox = n.ox + DX[e], oz = n.oz + DZ[e];  // |ox| + |oz| <= R: inside the grid
            if (seen[at(ox, oz)]) continue;
            seen[at(ox, oz)] = true;
            if (open_cell(w, q.x + ox, q.y, q.z + oz)) queue[tail++] = Node{ox, oz, n.dist + 1};
        }
    }
    return INT_MAX;
}

// Bit d set: water at q flows toward the cell beside it in direction d.
unsigned flow_dirs(const World& w, BlockPos q) {
    unsigned open = 0, nearest = 0;
    int best = INT_MAX;
    for (int d = 0; d < 4; ++d) {
        const std::size_t i = static_cast<std::size_t>(d);
        if (!open_cell(w, q.x + DX[i], q.y, q.z + DZ[i])) continue;
        open |= 1u << d;
        const int dist = drop_distance(w, q, d);
        if (dist < best) {
            best = dist;
            nearest = 1u << d;
        } else if (dist == best) {
            nearest |= 1u << d;
        }
    }
    return best <= FLUID_DROP_REACH ? nearest : open;
}

struct Cell {
    BlockId b;
    uint8_t meta;
};

// What the AIR or non-source WATER cell at p should hold now.
Cell desired(const World& w, BlockPos p, uint8_t meta) {
    if (p.y + 1 < WORLD_HEIGHT && is_water(w, p.x, p.y + 1, p.z)) return Cell{blk::WATER, WATER_FALLING};

    int sources = 0;
    for (std::size_t d = 0; d < 4; ++d) sources += is_source(w, p.x + DX[d], p.y, p.z + DZ[d]) ? 1 : 0;
    if (sources >= 2) {
        const BlockId below = w.get(p.x, p.y - 1, p.z);  // DEEPSTONE below the world
        if (block_def(below).solid || is_source(w, p.x, p.y - 1, p.z)) return Cell{blk::WATER, WATER_SOURCE};
    }

    const bool flowing = w.get(p.x, p.y, p.z) == blk::WATER && meta >= 1 && meta <= WATER_MAX_LEVEL;
    const int cap = flowing ? meta : WATER_MAX_LEVEL;  // the highest level a feeder may give
    int best = INT_MAX;
    for (std::size_t d = 0; d < 4; ++d) {
        const BlockPos q{p.x + DX[d], p.y, p.z + DZ[d]};
        if (!is_water(w, q.x, q.y, q.z)) continue;
        const int level = feed_level(w.meta(q.x, q.y, q.z)) + 1;
        if (level > cap || level >= best) continue;
        if (!spreads_sideways(w, q)) continue;
        if ((flow_dirs(w, q) & (1u << OPPOSITE[d])) == 0) continue;
        best = level;
    }
    if (best == INT_MAX) return Cell{blk::AIR, 0};
    return Cell{blk::WATER, static_cast<uint8_t>(best)};
}

}  // namespace

void fluid_update(World& w, TickScheduler& t, BlockPos p) {
    if (!loaded(w, p.x, p.y, p.z)) return;
    const BlockId cur = w.get(p.x, p.y, p.z);
    const uint8_t meta = w.meta(p.x, p.y, p.z);
    if (cur != blk::AIR && (cur != blk::WATER || meta == WATER_SOURCE)) return;
    const Cell want = desired(w, p, meta);
    if (want.b == cur && want.meta == meta) return;
    w.set(p.x, p.y, p.z, want.b, want.meta);

    t.schedule(BlockPos{p.x + 1, p.y, p.z}, FLUID_DELAY);
    t.schedule(BlockPos{p.x - 1, p.y, p.z}, FLUID_DELAY);
    t.schedule(BlockPos{p.x, p.y + 1, p.z}, FLUID_DELAY);
    t.schedule(BlockPos{p.x, p.y - 1, p.z}, FLUID_DELAY);
    t.schedule(BlockPos{p.x, p.y, p.z + 1}, FLUID_DELAY);
    t.schedule(BlockPos{p.x, p.y, p.z - 1}, FLUID_DELAY);
    if (want.b == blk::WATER && draining(w, p.x, p.y, p.z)) return;  // still falling: nothing landed
    int y = p.y + 1;
    while (y < WORLD_HEIGHT && is_flowing(w, p.x, y, p.z)) ++y;
    if (y < WORLD_HEIGHT && is_source(w, p.x, y, p.z))
        for (std::size_t d = 0; d < 4; ++d) t.schedule(BlockPos{p.x + DX[d], y, p.z + DZ[d]}, FLUID_DELAY);
}

void schedule_column_water(const World& w, TickScheduler& t, ColumnPos c) {
    if (!w.column_loaded(c)) return;
    const int x0 = c.x * CHUNK, z0 = c.z * CHUNK;
    const auto flowing = [](const Chunk& ch, int i) {
        return ch.blocks[static_cast<std::size_t>(i)] == blk::WATER && ch.meta[static_cast<std::size_t>(i)] != WATER_SOURCE;
    };
    for (int sy = 0; sy < SECTIONS; ++sy) {
        const Chunk* ch = w.chunk(ChunkPos{c.x, sy, c.z});
        for (int ly = 0; ly < CHUNK; ++ly)
            for (int lz = 0; lz < CHUNK; ++lz)
                for (int lx = 0; lx < CHUNK; ++lx) {
                    const bool border = lx == 0 || lx == CHUNK - 1 || lz == 0 || lz == CHUNK - 1;
                    const bool mine = flowing(*ch, Chunk::index(lx, ly, lz));
                    if (!mine && !border) continue;
                    const BlockPos p{x0 + lx, sy * CHUNK + ly, z0 + lz};
                    if (mine) t.schedule(p, FLUID_DELAY);
                    if (!border) continue;
                    // Across the border: water there that met this column while it was unloaded,
                    // or open cells there that this column's water could not reach.
                    for (std::size_t d = 0; d < 4; ++d) {
                        const int nlx = lx + DX[d], nlz = lz + DZ[d];
                        if (nlx >= 0 && nlx < CHUNK && nlz >= 0 && nlz < CHUNK) continue;
                        const Chunk* n = w.chunk(ChunkPos{c.x + DX[d], sy, c.z + DZ[d]});
                        if (n == nullptr) continue;
                        if (mine)
                            t.schedule(BlockPos{p.x + DX[d], p.y, p.z + DZ[d]}, FLUID_DELAY);
                        else if (flowing(*n, Chunk::index(floor_mod(nlx, CHUNK), ly, floor_mod(nlz, CHUNK))))
                            t.schedule(p, FLUID_DELAY);
                    }
                }
    }
}
