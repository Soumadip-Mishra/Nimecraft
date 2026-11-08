// Tests for src/light.* (M3 Task 2): sky and block light propagation, edits, column stitching.
// BFS rule (plan): light into a neighbor is max(0, l - 1 - opacity(neighbor)), except sky light
// moving straight down, which becomes l - opacity(neighbor) (unchanged through air).
#include "block.hpp"
#include "chunk.hpp"
#include "chunk_manager.hpp"
#include "coords.hpp"
#include "jobs.hpp"
#include "light.hpp"
#include "storage.hpp"
#include "terrain.hpp"
#include "test.hpp"
#include "world.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace {

using Gen = std::function<BlockId(int x, int y, int z)>;

BlockId open_air(int, int, int) {
    return blk::AIR;
}

// Builds column c from gen, lights it alone, inserts it, and stitches it to loaded neighbors
// (what ChunkManager does for a finished generation job).
void load(World& w, ColumnPos c, const Gen& gen) {
    Column col;
    for (int cy = 0; cy < SECTIONS; ++cy) {
        auto ch = std::make_unique<Chunk>();
        ch->pos = ChunkPos{c.x, cy, c.z};
        for (int ly = 0; ly < CHUNK; ++ly)
            for (int lz = 0; lz < CHUNK; ++lz)
                for (int lx = 0; lx < CHUNK; ++lx)
                    ch->blocks[static_cast<std::size_t>(Chunk::index(lx, ly, lz))] =
                        gen(c.x * CHUNK + lx, cy * CHUNK + ly, c.z * CHUNK + lz);
        col[static_cast<std::size_t>(cy)] = std::move(ch);
    }
    compute_column_light(col);
    w.insert_column(c, std::move(col));
    stitch_column_light(w, c);
}

void load_all(World& w, std::initializer_list<ColumnPos> cols, const Gen& gen) {
    for (ColumnPos c : cols) load(w, c, gen);
}

int sky(const World& w, int x, int y, int z) {
    return sky_light_at(w, x, y, z);
}

int bl(const World& w, int x, int y, int z) {
    return block_light_at(w, x, y, z);
}

int manhattan(int x, int y, int z, int cx, int cy, int cz) {
    return std::abs(x - cx) + std::abs(y - cy) + std::abs(z - cz);
}

// A 9x9x9 STONE shell (interior 7x7x7) centred on (cx, cy, cz).
struct Room {
    int cx, cy, cz;
    bool shell(int x, int y, int z) const {
        const int dx = std::abs(x - cx), dy = std::abs(y - cy), dz = std::abs(z - cz);
        return std::max({dx, dy, dz}) == 4;
    }
    bool inside(int x, int y, int z) const {
        return std::abs(x - cx) <= 3 && std::abs(y - cy) <= 3 && std::abs(z - cz) <= 3;
    }
    Gen gen() const {
        const Room r = *this;
        return [r](int x, int y, int z) { return r.shell(x, y, z) ? blk::STONE : blk::AIR; };
    }
    // Calls f(x, y, z) for every interior cell.
    template <class F>
    void each_inside(F f) const {
        for (int y = cy - 3; y <= cy + 3; ++y)
            for (int z = cz - 3; z <= cz + 3; ++z)
                for (int x = cx - 3; x <= cx + 3; ++x) f(x, y, z);
    }
};

// The columns a room centred on (cx, cz) touches.
std::vector<ColumnPos> room_columns(const Room& r) {
    std::vector<ColumnPos> out;
    for (int cz = floor_div(r.cz - 4, CHUNK); cz <= floor_div(r.cz + 4, CHUNK); ++cz)
        for (int cx = floor_div(r.cx - 4, CHUNK); cx <= floor_div(r.cx + 4, CHUNK); ++cx) out.push_back(ColumnPos{cx, cz});
    return out;
}

void load_room(World& w, const Room& r) {
    for (ColumnPos c : room_columns(r)) load(w, c, r.gen());
}

// The torch room checks: block light 14 - d inside, 0 in the shell and in a 2-cell band around it
// (cells of loaded columns), and no sky light inside the closed room.
void check_torch_room(const World& w, const Room& r) {
    r.each_inside([&](int x, int y, int z) {
        CHECK(bl(w, x, y, z) == 14 - manhattan(x, y, z, r.cx, r.cy, r.cz));
        CHECK(sky(w, x, y, z) == 0);
    });
    for (int y = r.cy - 6; y <= r.cy + 6; ++y)
        for (int z = r.cz - 6; z <= r.cz + 6; ++z)
            for (int x = r.cx - 6; x <= r.cx + 6; ++x)
                if (!r.inside(x, y, z)) CHECK(bl(w, x, y, z) == 0);
}

// Every cell of the box obeys the BFS rule against its 6 neighbors in loaded columns: its light is
// at least what each neighbor sends it, and an opaque cell holds none.
bool consistent(const World& w, int x0, int y0, int z0, int x1, int y1, int z1) {
    static constexpr int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                if (!w.column_loaded(ColumnPos{floor_div(x, CHUNK), floor_div(z, CHUNK)})) continue;
                const int op = block_def(w.get(x, y, z)).opacity;
                const int s = sky(w, x, y, z), b = bl(w, x, y, z);
                if (op == 15 && (s != 0 || b != 0)) return false;
                for (const auto& d : D) {
                    const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                    if (ny < 0 || ny >= WORLD_HEIGHT) continue;
                    if (!w.column_loaded(ColumnPos{floor_div(nx, CHUNK), floor_div(nz, CHUNK)})) continue;
                    const int ns = sky(w, nx, ny, nz), nb = bl(w, nx, ny, nz);
                    // The neighbor sends light to (x, y, z); down for it is d = (0, -1, 0) seen from
                    // the neighbor, which sits above when d[1] == 1.
                    const int s_in = d[1] == 1 ? ns - op : ns - 1 - op;
                    if (s < std::max(0, s_in)) return false;
                    if (b < std::max(0, nb - 1 - op)) return false;
                }
            }
    return true;
}

bool has(const std::vector<ChunkPos>& v, ChunkPos p) {
    return std::find(v.begin(), v.end(), p) != v.end();
}

}  // namespace

TEST(test_light_torch_room) {
    World w(1);
    const Room r{8, 80, 8};
    load_room(w, r);
    CHECK(w.set(8, 80, 8, blk::TORCH));
    check_torch_room(w, r);
    CHECK(bl(w, 8, 80, 8) == 14);
    CHECK(bl(w, 8, 80, 5) == 11);
    CHECK(bl(w, 5, 77, 5) == 5);  // the farthest corner, d = 9
}

TEST(test_light_torch_removed) {
    World w(1);
    const Room r{8, 80, 8};
    load_room(w, r);
    w.set(8, 80, 8, blk::TORCH);
    w.set(8, 80, 8, blk::AIR);
    r.each_inside([&](int x, int y, int z) {
        CHECK(bl(w, x, y, z) == 0);
        CHECK(sky(w, x, y, z) == 0);
    });
}

TEST(test_light_open_roof) {
    World w(1);
    const Room r{8, 80, 8};
    load_room(w, r);
    CHECK(sky(w, 8, 83, 8) == 0);
    CHECK(w.set(8, 84, 8, blk::AIR));  // the roof block above the centre
    CHECK(sky(w, 8, 84, 8) == 15);
    CHECK(sky(w, 8, 83, 8) == 15);  // the cell below the hole
    CHECK(sky(w, 8, 77, 8) == 15);  // straight down to the floor, unchanged through air
    CHECK(sky(w, 8, 76, 8) == 0);   // the STONE floor itself
    CHECK(sky(w, 7, 77, 8) == 14);  // one step sideways
    CHECK(consistent(w, 2, 74, 2, 14, 86, 14));
}

TEST(test_light_place_blocker) {
    World w(1);
    load(w, ColumnPos{0, 0}, [](int, int y, int) { return y <= 64 ? blk::STONE : blk::AIR; });
    CHECK(sky(w, 8, 65, 8) == 15);
    CHECK(w.set(8, 66, 8, blk::STONE));
    CHECK(sky(w, 8, 65, 8) < 15);
    CHECK(sky(w, 8, 65, 8) == 14);  // lit sideways now
    CHECK(sky(w, 8, 66, 8) == 0);
    CHECK(consistent(w, 0, 60, 0, 15, 70, 15));
}

TEST(test_light_across_column_border) {
    World w(1);
    load_all(w, {ColumnPos{0, 0}, ColumnPos{1, 0}}, open_air);
    CHECK(w.set(15, 80, 8, blk::TORCH));
    CHECK(bl(w, 16, 80, 8) == 13);
    CHECK(bl(w, 17, 80, 8) == 12);
    CHECK(bl(w, 17, 81, 9) == 10);
    w.set(15, 80, 8, blk::AIR);  // and removal crosses back
    CHECK(bl(w, 17, 80, 8) == 0);
}

TEST(test_light_stitch_on_late_load) {
    World w(1);
    load(w, ColumnPos{0, 0}, open_air);
    CHECK(w.set(15, 80, 8, blk::TORCH));
    CHECK(bl(w, 16, 80, 8) == 0);  // unloaded
    load(w, ColumnPos{1, 0}, open_air);
    CHECK(bl(w, 16, 80, 8) == 13);
    CHECK(bl(w, 17, 80, 8) == 12);
    CHECK(bl(w, 16, 80, 2) == 7);
    CHECK(consistent(w, 10, 70, 0, 25, 90, 15));
}

TEST(test_light_stitch_both_directions) {
    // A torch in the column that loads second also lights the column already loaded.
    World w(1);
    load(w, ColumnPos{0, 0}, open_air);
    load(w, ColumnPos{1, 0}, [](int x, int y, int z) { return x == 16 && y == 80 && z == 8 ? blk::TORCH : blk::AIR; });
    CHECK(bl(w, 16, 80, 8) == 14);
    CHECK(bl(w, 15, 80, 8) == 13);
    CHECK(bl(w, 12, 80, 8) == 10);
}

TEST(test_light_leaves_and_water_dim) {
    {
        World w(1);
        load(w, ColumnPos{0, 0}, [](int, int y, int) { return y >= 100 && y <= 102 ? blk::LEAVES : blk::AIR; });
        CHECK(sky(w, 8, 102, 8) == 14);
        CHECK(sky(w, 8, 101, 8) == 13);
        CHECK(sky(w, 8, 99, 8) == 12);
        CHECK(sky(w, 8, 40, 8) == 12);  // unchanged through air below
        CHECK(sky(w, 0, 99, 0) == 12);  // the column's corner too
    }
    {
        World w(1);
        load(w, ColumnPos{0, 0}, [](int, int y, int) { return y >= 100 && y <= 102 ? blk::WATER : blk::AIR; });
        CHECK(sky(w, 8, 99, 8) == 9);
        CHECK(sky(w, 8, 100, 8) == 9);
        CHECK(sky(w, 8, 102, 8) == 13);
    }
}

TEST(test_light_two_torches_max) {
    World w(1);
    const Room r{8, 80, 8};
    load_room(w, r);
    w.set(5, 80, 8, blk::TORCH);
    w.set(11, 80, 8, blk::TORCH);
    CHECK(bl(w, 8, 80, 8) == 11);  // the midpoint: the maximum, not the sum
    r.each_inside([&](int x, int y, int z) {
        const int want = std::max(14 - manhattan(x, y, z, 5, 80, 8), 14 - manhattan(x, y, z, 11, 80, 8));
        CHECK(bl(w, x, y, z) == want);
    });
}

TEST(test_light_remove_one_of_two) {
    World w(1);
    const Room r{8, 80, 8};
    load_room(w, r);
    w.set(5, 80, 8, blk::TORCH);
    w.set(11, 80, 8, blk::TORCH);
    w.set(5, 80, 8, blk::AIR);
    r.each_inside([&](int x, int y, int z) {
        CHECK(bl(w, x, y, z) == std::max(0, 14 - manhattan(x, y, z, 11, 80, 8)));
    });
}

TEST(test_light_torch_replaced_by_stone) {
    World w(1);
    const Room r{8, 80, 8};
    load_room(w, r);
    w.set(8, 80, 8, blk::TORCH);
    CHECK(w.set(8, 80, 8, blk::STONE));
    r.each_inside([&](int x, int y, int z) { CHECK(bl(w, x, y, z) == 0); });
}

TEST(test_light_lantern) {
    World w(1);
    load(w, ColumnPos{0, 0}, open_air);
    w.set(8, 80, 8, blk::LANTERN);
    CHECK(bl(w, 8, 80, 8) == 15);
    CHECK(bl(w, 9, 80, 8) == 14);
    CHECK(bl(w, 7, 80, 8) == 14);
    CHECK(bl(w, 8, 81, 8) == 14);
    CHECK(bl(w, 8, 79, 8) == 14);
    CHECK(bl(w, 8, 80, 9) == 14);
    CHECK(bl(w, 8, 80, 7) == 14);
    CHECK(sky(w, 8, 80, 8) == 15);  // opacity 0: the lantern does not block sky light
}

TEST(test_light_glowcap) {
    World w(1);
    load(w, ColumnPos{0, 0}, open_air);
    w.set(8, 80, 8, blk::GLOWCAP);
    CHECK(bl(w, 8, 80, 8) == 7);
    CHECK(bl(w, 8, 80, 2) == 1);
    CHECK(bl(w, 8, 80, 1) == 0);
}

TEST(test_light_negative_coords) {
    World w(1);
    const Room r{-20, 80, -20};  // spans columns -2 and -1 on both axes
    load_room(w, r);
    CHECK(room_columns(r).size() == 4);
    CHECK(w.set(-20, 80, -20, blk::TORCH));
    check_torch_room(w, r);
}

TEST(test_light_far_coords) {
    World w(1);
    const Room r{1000000, 80, -1000000};
    load_room(w, r);
    CHECK(w.set(1000000, 80, -1000000, blk::TORCH));
    check_torch_room(w, r);
}

TEST(test_light_vertical_chunk_border) {
    World w(1);
    load(w, ColumnPos{0, 0}, open_air);
    CHECK(w.set(8, 79, 8, blk::TORCH));  // top cell of section 4
    CHECK(bl(w, 8, 80, 8) == 13);
    CHECK(bl(w, 8, 81, 8) == 12);
    CHECK(bl(w, 8, 76, 8) == 11);
}

TEST(test_light_world_top) {
    World w(1);
    load(w, ColumnPos{0, 0}, open_air);
    CHECK(sky(w, 0, 255, 0) == 15);
    CHECK(w.set(0, 255, 0, blk::STONE));
    CHECK(sky(w, 0, 255, 0) == 0);
    CHECK(sky(w, 0, 254, 0) == 14);
    CHECK(sky(w, 0, 100, 0) == 14);  // straight down from 14, unchanged through air
    CHECK(w.set(0, 255, 0, blk::AIR));
    CHECK(sky(w, 0, 254, 0) == 15);
    CHECK(sky(w, 0, 100, 0) == 15);
    // A torch at the top and one at the bottom of the world.
    CHECK(w.set(5, 255, 5, blk::TORCH));
    CHECK(bl(w, 5, 254, 5) == 13);
    CHECK(w.set(5, 0, 5, blk::TORCH));
    CHECK(bl(w, 5, 1, 5) == 13);
    CHECK(consistent(w, 0, 240, 0, 15, 255, 15));
    CHECK(consistent(w, 0, 0, 0, 15, 15, 15));
}

TEST(test_light_unloaded_neighbor) {
    World w(1);
    load(w, ColumnPos{0, 0}, open_air);
    CHECK(w.set(15, 80, 8, blk::TORCH));
    CHECK(bl(w, 15, 80, 8) == 14);
    CHECK(bl(w, 14, 80, 8) == 13);
    CHECK(bl(w, 15, 80, 0) == 6);
    CHECK(bl(w, 16, 80, 8) == 0);
    CHECK(!w.column_loaded(ColumnPos{1, 0}));
    CHECK(w.loaded_columns().size() == 1);
    CHECK(w.set(15, 80, 8, blk::AIR));
    CHECK(bl(w, 14, 80, 8) == 0);
    CHECK(w.loaded_columns().size() == 1);
}

TEST(test_light_open_sky_column) {
    for (ColumnPos c : {ColumnPos{0, 0}, ColumnPos{-3, 5}, ColumnPos{12, -7}}) {
        Column col = generate_column(7, c);
        compute_column_light(col);
        for (int lz = 0; lz < CHUNK; ++lz)
            for (int lx = 0; lx < CHUNK; ++lx) {
                int top = WORLD_HEIGHT - 1;
                while (top > 0 && col[static_cast<std::size_t>(top / CHUNK)]->get(lx, top % CHUNK, lz) == blk::AIR) --top;
                for (int y = top + 1; y < WORLD_HEIGHT; ++y)
                    CHECK(sky_light(*col[static_cast<std::size_t>(y / CHUNK)], Chunk::index(lx, y % CHUNK, lz)) == 15);
                // Opaque cells hold no light.
                for (int y = 0; y <= top; ++y) {
                    const Chunk& ch = *col[static_cast<std::size_t>(y / CHUNK)];
                    const int i = Chunk::index(lx, y % CHUNK, lz);
                    if (block_def(ch.blocks[static_cast<std::size_t>(i)]).opaque)
                        CHECK(ch.light[static_cast<std::size_t>(i)] == 0);
                }
            }
    }
}

TEST(test_compute_column_light_resets) {
    // compute_column_light starts from dark: stale light in the input is dropped.
    Column col;
    for (int cy = 0; cy < SECTIONS; ++cy) {
        col[static_cast<std::size_t>(cy)] = std::make_unique<Chunk>();
        col[static_cast<std::size_t>(cy)]->blocks.fill(blk::STONE);
        col[static_cast<std::size_t>(cy)]->light.fill(0xFF);
    }
    compute_column_light(col);
    for (const auto& ch : col) CHECK(std::all_of(ch->light.begin(), ch->light.end(), [](uint8_t v) { return v == 0; }));
}

TEST(test_compute_column_light_concurrent) {
    constexpr std::size_t N = 8;
    std::array<Column, N> serial, parallel;
    for (std::size_t i = 0; i < N; ++i) {
        const ColumnPos c{static_cast<int>(i) - 4, static_cast<int>(i % 3)};
        serial[i] = generate_column(7, c);
        parallel[i] = generate_column(7, c);
        compute_column_light(serial[i]);
    }
    {
        ThreadPool pool(4);
        for (std::size_t i = 0; i < N; ++i) pool.submit([&parallel, i] { compute_column_light(parallel[i]); });
        pool.wait_idle();
    }
    for (std::size_t i = 0; i < N; ++i)
        for (std::size_t s = 0; s < SECTIONS; ++s) CHECK(serial[i][s]->light == parallel[i][s]->light);
}

// ---- Coverage Checklist additions ----

TEST(test_light_same_props_edit_keeps_light) {
    // GRASS -> DIRT changes neither opacity nor emission: light stays, only the edited chunk is dirty.
    World w(1);
    load(w, ColumnPos{0, 0}, [](int, int y, int) { return y <= 64 ? blk::GRASS : blk::AIR; });
    std::vector<std::array<uint8_t, 4096>> before;
    for (int cy = 0; cy < SECTIONS; ++cy) before.push_back(w.chunk(ChunkPos{0, cy, 0})->light);
    w.take_dirty();
    CHECK(w.set(8, 40, 8, blk::DIRT));
    for (int cy = 0; cy < SECTIONS; ++cy) CHECK(w.chunk(ChunkPos{0, cy, 0})->light == before[static_cast<std::size_t>(cy)]);
    const std::vector<ChunkPos> dirty = w.take_dirty();
    CHECK(dirty.size() == 1);
    CHECK(has(dirty, ChunkPos{0, 2, 0}));
}

TEST(test_light_edit_marks_light_changed_chunks_dirty) {
    // A torch at a chunk corner lights cells in the 8 chunks around that corner; each is dirty,
    // including the diagonal ones, and a chunk the light never reaches is not.
    World w(1);
    load_all(w, {ColumnPos{0, 0}, ColumnPos{1, 0}, ColumnPos{0, 1}, ColumnPos{1, 1}}, open_air);
    w.take_dirty();
    CHECK(w.set(15, 79, 15, blk::TORCH));
    const std::vector<ChunkPos> dirty = w.take_dirty();
    for (int cy = 4; cy <= 5; ++cy)
        for (int cz = 0; cz <= 1; ++cz)
            for (int cx = 0; cx <= 1; ++cx) CHECK(has(dirty, ChunkPos{cx, cy, cz}));
    CHECK(!has(dirty, ChunkPos{0, 8, 0}));
    CHECK(!has(dirty, ChunkPos{0, 0, 0}));
}

TEST(test_light_stitch_marks_dirty) {
    // A torch at the near border of the column that loads second lights the loaded column: its
    // chunk is dirty; a chunk whose light the stitch does not change is not.
    World w(1);
    load(w, ColumnPos{0, 0}, open_air);
    w.take_dirty();
    load(w, ColumnPos{1, 0}, [](int x, int y, int z) { return x == 16 && y == 80 && z == 8 ? blk::TORCH : blk::AIR; });
    const std::vector<ChunkPos> dirty = w.take_dirty();
    CHECK(has(dirty, ChunkPos{0, 5, 0}));
    CHECK(!has(dirty, ChunkPos{0, 0, 0}));
    CHECK(!has(dirty, ChunkPos{0, 10, 0}));
}

TEST(test_light_stitch_without_neighbors) {
    // Stitching an absent column, or one with no loaded neighbor, changes nothing.
    World w(1);
    stitch_column_light(w, ColumnPos{3, 3});
    CHECK(w.loaded_columns().empty());
    load(w, ColumnPos{0, 0}, [](int x, int y, int z) { return x == 15 && y == 80 && z == 8 ? blk::TORCH : blk::AIR; });
    w.take_dirty();
    stitch_column_light(w, ColumnPos{0, 0});
    CHECK(w.take_dirty().empty());
    CHECK(bl(w, 15, 80, 8) == 14);
}

TEST(test_light_readers_outside) {
    // Outside the world or in unloaded columns: no block light; sky 15 above the world, 0 below
    // and in unloaded columns.
    World w(1);
    load(w, ColumnPos{0, 0}, open_air);
    CHECK(sky_light_at(w, 0, 256, 0) == 15);
    CHECK(sky_light_at(w, 0, -1, 0) == 0);
    CHECK(sky_light_at(w, 16, 100, 0) == 0);
    CHECK(block_light_at(w, 0, 256, 0) == 0);
    CHECK(block_light_at(w, 0, -1, 0) == 0);
    CHECK(block_light_at(w, -1, 100, 0) == 0);
    CHECK(sky_light_at(w, 0, 255, 0) == 15);
}

TEST(test_light_cave_shaft_and_overhang) {
    // Ground at 64 with a 1x1 shaft down to y 20 and a cave room at y 20..23: the shaft carries 15
    // straight down; the room beside its foot is lit sideways (14, 13, ...).
    World w(1);
    load(w, ColumnPos{0, 0}, [](int x, int y, int z) {
        if (y > 64) return blk::AIR;
        if (x == 8 && z == 8 && y >= 20) return blk::AIR;           // shaft
        if (y >= 20 && y <= 23 && x >= 2 && x <= 14 && z == 8) return blk::AIR;  // tunnel
        return blk::STONE;
    });
    CHECK(sky(w, 8, 20, 8) == 15);
    CHECK(sky(w, 9, 20, 8) == 14);
    CHECK(sky(w, 12, 20, 8) == 11);
    CHECK(sky(w, 12, 23, 8) == 11);
    CHECK(sky(w, 12, 21, 8) == 11);  // 12, 23 sends 11 straight down through air
    CHECK(consistent(w, 0, 15, 0, 15, 70, 15));
    // Cover the shaft: the whole cave goes dark.
    CHECK(w.set(8, 64, 8, blk::STONE));
    CHECK(sky(w, 8, 20, 8) == 0);
    CHECK(sky(w, 12, 21, 8) == 0);
    CHECK(sky(w, 8, 63, 8) == 0);
    CHECK(consistent(w, 0, 15, 0, 15, 70, 15));
}

TEST(test_light_manager_lights_columns) {
    // Streamed columns arrive lit (compute in the job) and stitched (consistent across borders).
    World w(7);
    {
        ThreadPool pool(4);
        ChunkManager m(w, pool, nullptr, 2);
        m.settle(ColumnPos{0, 0});
    }
    for (int lz = 0; lz < CHUNK; ++lz)
        for (int lx = 0; lx < CHUNK; ++lx) CHECK(sky(w, lx, 255, lz) == 15);
    CHECK(consistent(w, 12, 0, 0, 19, 255, 15));    // the border between columns 0 and 1
    CHECK(consistent(w, -4, 0, -4, 3, 255, 3));     // the corner of four columns
}

TEST(test_light_manager_lights_saved_columns) {
    // A column loaded from the region store is lit too: STONE up to 249 (no generated column is
    // that high) has sky 15 above it and 0 below.
    test::TempDir dir;
    {
        RegionStore store(dir.path());
        Column col;
        for (int cy = 0; cy < SECTIONS; ++cy) {
            col[static_cast<std::size_t>(cy)] = std::make_unique<Chunk>();
            for (int ly = 0; ly < CHUNK; ++ly)
                if (cy * CHUNK + ly <= 249)
                    for (int i = 0; i < CHUNK * CHUNK; ++i)
                        col[static_cast<std::size_t>(cy)]->blocks[static_cast<std::size_t>(ly * 256 + i)] = blk::STONE;
        }
        store.save_column(ColumnPos{0, 0}, col);
        CHECK(store.flush());
    }
    World w(7);
    RegionStore store(dir.path());
    {
        ThreadPool pool(2);
        ChunkManager m(w, pool, &store, 1);
        m.settle(ColumnPos{0, 0});
    }
    CHECK(w.get(8, 249, 8) == blk::STONE);
    CHECK(sky(w, 8, 250, 8) == 15);
    CHECK(sky(w, 8, 249, 8) == 0);
    CHECK(sky(w, 8, 200, 8) == 0);
}

TEST(test_light_unchanged_light_not_dirty) {
    // Under a roof, STONE placed mid-air takes the cell from sky 14 to 0; the cells below are
    // cleared and refilled to the same 14 from the side, so only the edited chunk is dirty.
    World w(1);
    load_all(w, {ColumnPos{0, 0}, ColumnPos{-1, 0}, ColumnPos{0, -1}}, open_air);
    CHECK(w.set(1, 200, 1, blk::STONE));
    CHECK(sky(w, 1, 100, 1) == 14);
    CHECK(sky(w, 1, 20, 1) == 14);
    w.take_dirty();
    CHECK(w.set(1, 100, 1, blk::STONE));
    CHECK(sky(w, 1, 99, 1) == 14);
    CHECK(sky(w, 1, 100, 1) == 0);
    const std::vector<ChunkPos> dirty = w.take_dirty();
    CHECK(dirty.size() == 1);
    CHECK(has(dirty, ChunkPos{0, 6, 0}));
}

TEST(test_light_weaker_emitter_survives_removal) {
    // A GLOWCAP (7) two cells from a LANTERN (15) reads 13. Removing the lantern clears it back to
    // its own 7, and it must light its surroundings again: 6 next to it.
    World w(1);
    load(w, ColumnPos{0, 0}, open_air);
    w.set(8, 80, 8, blk::LANTERN);
    w.set(10, 80, 8, blk::GLOWCAP);
    CHECK(bl(w, 10, 80, 8) == 13);
    CHECK(w.set(8, 80, 8, blk::AIR));
    CHECK(bl(w, 10, 80, 8) == 7);
    CHECK(bl(w, 11, 80, 8) == 6);
    CHECK(bl(w, 10, 80, 11) == 4);
    CHECK(bl(w, 8, 80, 8) == 5);
}

TEST(test_light_stitch_order_independent) {
    // Light is the same whichever of two neighbors loads first (exact arrays, so light that is
    // too bright would show too).
    const Gen gen = [](int x, int y, int z) {
        if (x == 15 && y == 70 && z == 3) return blk::TORCH;
        if (x == 17 && y == 72 && z == 9) return blk::LANTERN;
        if (y <= 64 || (x == 16 && y < 90 && z > 6)) return blk::STONE;
        return blk::AIR;
    };
    World ab(1), ba(1);
    load(ab, ColumnPos{0, 0}, gen);
    load(ab, ColumnPos{1, 0}, gen);
    load(ba, ColumnPos{1, 0}, gen);
    load(ba, ColumnPos{0, 0}, gen);
    for (int cx = 0; cx <= 1; ++cx)
        for (int cy = 0; cy < SECTIONS; ++cy)
            CHECK(ab.chunk(ChunkPos{cx, cy, 0})->light == ba.chunk(ChunkPos{cx, cy, 0})->light);
    CHECK(bl(ab, 16, 70, 3) == 13);
}

TEST(test_light_edit_with_neighbor_column_source) {
    // A torch in column 1 lights column 0; a STONE wall placed in column 0 cuts that light off
    // behind it, and removing the wall lets it back in.
    World w(1);
    load_all(w, {ColumnPos{0, 0}, ColumnPos{1, 0}}, [](int x, int y, int z) {
        if (x == 16 && y == 80 && z == 8) return blk::TORCH;
        // A tunnel x <= 15 at y 80, z 8 inside STONE, so the only way west is through (15, 80, 8).
        if (x <= 15 && !(y == 80 && z == 8)) return blk::STONE;
        return blk::AIR;
    });
    CHECK(bl(w, 15, 80, 8) == 13);
    CHECK(bl(w, 14, 80, 8) == 12);
    CHECK(w.set(15, 80, 8, blk::STONE));
    CHECK(bl(w, 15, 80, 8) == 0);
    CHECK(bl(w, 14, 80, 8) == 0);
    CHECK(bl(w, 10, 80, 8) == 0);
    CHECK(w.set(15, 80, 8, blk::AIR));
    CHECK(bl(w, 14, 80, 8) == 12);
    CHECK(bl(w, 10, 80, 8) == 8);
}

TEST(test_regress_stitch_seeds_only_crossing_light) {
    // Stitching seeds only the border cells whose light crosses into the neighbor (the fix for
    // frame spikes: seeding every lit border cell cost about 1.4 ms per arriving column on the main
    // thread). This guards that the narrower seeding gives the same light, not the timing itself:
    // real terrain loaded in two orders must end with identical light, consistent everywhere.
    const auto load_generated = [](World& w, ColumnPos c) {
        Column col = generate_column(w.seed(), c);
        compute_column_light(col);
        w.insert_column(c, std::move(col));
        stitch_column_light(w, c);
    };
    World rows(42), ring(42);
    for (int cz = -1; cz <= 1; ++cz)
        for (int cx = -1; cx <= 1; ++cx) load_generated(rows, ColumnPos{cx, cz});
    // Corners first, then edges, then the middle: every border is stitched from the other side.
    for (const ColumnPos c : {ColumnPos{-1, -1}, ColumnPos{1, 1}, ColumnPos{1, -1}, ColumnPos{-1, 1},
                              ColumnPos{0, -1}, ColumnPos{0, 1}, ColumnPos{-1, 0}, ColumnPos{1, 0},
                              ColumnPos{0, 0}})
        load_generated(ring, c);
    for (int cz = -1; cz <= 1; ++cz)
        for (int cx = -1; cx <= 1; ++cx)
            for (int cy = 0; cy < SECTIONS; ++cy)
                CHECK(rows.chunk(ChunkPos{cx, cy, cz})->light == ring.chunk(ChunkPos{cx, cy, cz})->light);
    CHECK(consistent(rows, -16, 0, -16, 31, 255, 31));
}
