// Tests for src/fluids.*: flowing water (spread, fall, drops, recede, infinite sources, borders,
// unloaded neighbors) and the water heights in the mesh.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "fluids.hpp"
#include "mesher.hpp"
#include "test.hpp"
#include "ticks.hpp"
#include "world.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

constexpr int FLOOR = 63;  // STONE floor; water runs on y = 64

// Loads the columns cx0..cx1 x cz0..cz1 with a STONE floor at y = FLOOR.
void load_flat(World& w, int cx0, int cx1, int cz0, int cz1) {
    for (int cz = cz0; cz <= cz1; ++cz)
        for (int cx = cx0; cx <= cx1; ++cx) {
            w.insert_column(ColumnPos{cx, cz}, Column{});
            for (int z = 0; z < CHUNK; ++z)
                for (int x = 0; x < CHUNK; ++x) w.set(cx * CHUNK + x, FLOOR, cz * CHUNK + z, blk::STONE);
        }
}

// The 3 x 3 columns around (0, 0): x and z in [-16, 32).
World flat_world() {
    World w(7);
    load_flat(w, -1, 1, -1, 1);
    return w;
}

struct Sim {
    World& w;
    TickScheduler t;
    uint64_t now = 0;

    explicit Sim(World& world) : w(world) {}
    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            ++now;
            for (BlockPos p : t.due(now)) fluid_update(w, t, p);
        }
    }
    void place_source(BlockPos p) {
        w.set(p.x, p.y, p.z, blk::WATER);
        schedule_around(t, p, FLUID_DELAY);
    }
    void remove(BlockPos p) {
        w.set(p.x, p.y, p.z, blk::AIR);
        schedule_around(t, p, FLUID_DELAY);
    }
};

bool water(const World& w, int x, int y, int z) { return w.get(x, y, z) == blk::WATER; }

// The level of a flowing cell, 0 for a source, -1 when the cell holds no water.
int level(const World& w, int x, int y, int z) {
    return water(w, x, y, z) ? static_cast<int>(w.meta(x, y, z)) : -1;
}

int water_count(const World& w, int x0, int x1, int y0, int y1, int z0, int z1) {
    int n = 0;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) n += water(w, x, y, z) ? 1 : 0;
    return n;
}

float max_y(const MeshData& m) {
    float top = -1e9f;
    for (std::size_t i = 1; i < m.positions.size(); i += 3) top = std::max(top, m.positions[i]);
    return top;
}

}  // namespace

TEST(test_water_spreads_7) {
    World w = flat_world();
    Sim s(w);
    s.place_source(BlockPos{8, 64, 8});
    s.run(400);
    CHECK(level(w, 8, 64, 8) == 0);
    bool ok = true;
    for (int dz = -9; dz <= 9; ++dz)
        for (int dx = -9; dx <= 9; ++dx) {
            const int d = std::abs(dx) + std::abs(dz);
            if (d == 0) continue;
            const int want = d <= 7 ? d : -1;
            ok = ok && level(w, 8 + dx, 64, 8 + dz) == want;
        }
    CHECK(ok);
    CHECK(level(w, 15, 64, 8) == 7);
    CHECK(level(w, 16, 64, 8) == -1);  // distance 8
    CHECK(level(w, 4, 64, 4) == -1);   // distance 8 diagonally
    CHECK(water_count(w, -16, 31, 65, 70, -16, 31) == 0);  // nothing climbs
}

TEST(test_water_falls_first) {
    World w = flat_world();
    for (int y = 50; y < FLOOR; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) w.set(x, y, z, blk::STONE);
    for (int y = 59; y <= 63; ++y) w.set(8, y, 8, blk::AIR);  // a 1 x 1 hole 5 deep, bottom at 58
    Sim s(w);
    s.place_source(BlockPos{8, 64, 8});
    int bottom_at = -1, side_at = -1;
    for (int i = 1; i <= 200; ++i) {
        s.run(1);
        if (bottom_at < 0 && water(w, 8, 59, 8)) bottom_at = i;
        if (side_at < 0 && (water(w, 9, 64, 8) || water(w, 7, 64, 8) || water(w, 8, 64, 9) || water(w, 8, 64, 7)))
            side_at = i;
    }
    CHECK(bottom_at > 0);
    CHECK(side_at > 0);
    CHECK(bottom_at < side_at);
    bool falling = true;
    for (int y = 59; y <= 63; ++y) falling = falling && level(w, 8, y, 8) == 8;
    CHECK(falling);
    CHECK(level(w, 9, 64, 8) == 1);  // once the hole is full the source spreads
    CHECK(level(w, 10, 64, 8) == 2);
}

TEST(test_water_falls_one_cell_per_step) {
    World w = flat_world();
    w.set(8, FLOOR, 8, blk::AIR);
    w.set(8, FLOOR - 1, 8, blk::AIR);  // floor of the hole at 61
    w.set(8, FLOOR - 2, 8, blk::STONE);
    Sim s(w);
    s.place_source(BlockPos{8, 64, 8});
    s.run(4);
    CHECK(!water(w, 8, 63, 8));
    s.run(1);
    CHECK(level(w, 8, 63, 8) == 8);
    CHECK(!water(w, 8, 62, 8));
    s.run(5);
    CHECK(level(w, 8, 62, 8) == 8);
}

TEST(test_water_prefers_drop) {
    World w = flat_world();
    w.set(11, FLOOR, 8, blk::AIR);  // a drop 3 cells to +X
    w.set(11, FLOOR - 1, 8, blk::STONE);
    Sim s(w);
    s.place_source(BlockPos{8, 64, 8});
    s.run(400);
    CHECK(level(w, 9, 64, 8) == 1);
    CHECK(level(w, 10, 64, 8) == 2);
    CHECK(level(w, 11, 64, 8) == 3);
    CHECK(level(w, 11, 63, 8) == 8);
    CHECK(!water(w, 7, 64, 8));
    CHECK(!water(w, 8, 64, 9));
    CHECK(!water(w, 8, 64, 7));
    CHECK(!water(w, 12, 64, 8));  // water over falling water does not spread
    CHECK(water_count(w, -16, 31, 64, 64, -16, 31) == 4);
}

TEST(test_water_drop_within_4) {
    {
        World w = flat_world();
        w.set(12, FLOOR, 8, blk::AIR);  // exactly 4 cells to +X: steers the flow
        w.set(12, FLOOR - 1, 8, blk::STONE);
        Sim s(w);
        s.place_source(BlockPos{8, 64, 8});
        s.run(400);
        CHECK(level(w, 9, 64, 8) == 1);
        CHECK(level(w, 12, 64, 8) == 4);
        CHECK(!water(w, 7, 64, 8));
        CHECK(!water(w, 8, 64, 9));
    }
    {
        World w = flat_world();
        w.set(13, FLOOR, 8, blk::AIR);  // 5 cells to +X: too far to steer the flow
        w.set(13, FLOOR - 1, 8, blk::STONE);
        Sim s(w);
        s.place_source(BlockPos{8, 64, 8});
        s.run(400);
        CHECK(level(w, 7, 64, 8) == 1);
        CHECK(level(w, 8, 64, 9) == 1);
        CHECK(level(w, 8, 64, 7) == 1);
        CHECK(level(w, 9, 64, 8) == 1);
    }
}

TEST(test_water_recedes) {
    World w = flat_world();
    Sim s(w);
    s.place_source(BlockPos{8, 64, 8});
    s.run(200);
    CHECK(water_count(w, -16, 31, 64, 64, -16, 31) == 113);  // 1 + 4 + 8 + ... + 28
    s.remove(BlockPos{8, 64, 8});
    s.run(200);
    CHECK(water_count(w, -16, 31, 60, 70, -16, 31) == 0);
    CHECK(s.t.size() == 0);
}

TEST(test_water_recedes_falling_column) {
    World w = flat_world();
    for (int y = 60; y <= FLOOR; ++y) w.set(8, y, 8, blk::AIR);
    w.set(8, 59, 8, blk::STONE);
    Sim s(w);
    s.place_source(BlockPos{8, 64, 8});
    s.run(200);
    CHECK(level(w, 8, 60, 8) == 8);
    s.remove(BlockPos{8, 64, 8});
    s.run(200);
    CHECK(water_count(w, -16, 31, 55, 70, -16, 31) == 0);
}

TEST(test_infinite_source_rule) {
    {
        World w = flat_world();
        Sim s(w);
        s.place_source(BlockPos{7, 64, 8});
        s.place_source(BlockPos{9, 64, 8});
        s.run(200);
        CHECK(level(w, 8, 64, 8) == 0);
        // It stays a source after both neighbors are gone.
        s.remove(BlockPos{7, 64, 8});
        s.remove(BlockPos{9, 64, 8});
        s.run(200);
        CHECK(level(w, 8, 64, 8) == 0);
    }
    {
        World w = flat_world();
        for (int y = 60; y <= FLOOR; ++y) w.set(8, y, 8, blk::AIR);  // AIR under the gap
        w.set(8, 59, 8, blk::STONE);
        Sim s(w);
        s.place_source(BlockPos{7, 64, 8});
        s.place_source(BlockPos{9, 64, 8});
        s.run(200);
        CHECK(level(w, 8, 64, 8) == 1);
        CHECK(level(w, 8, 63, 8) == 8);
    }
    {
        // One source neighbor is not enough.
        World w = flat_world();
        Sim s(w);
        s.place_source(BlockPos{7, 64, 8});
        s.run(200);
        CHECK(level(w, 8, 64, 8) == 1);
    }
    {
        // Over a source the gap becomes a source too (a second layer of a pool).
        World w = flat_world();
        Sim s(w);
        s.place_source(BlockPos{8, 64, 8});
        s.place_source(BlockPos{7, 65, 8});
        s.place_source(BlockPos{9, 65, 8});
        w.set(7, 64, 8, blk::STONE);
        w.set(9, 64, 8, blk::STONE);
        s.run(200);
        CHECK(level(w, 8, 65, 8) == 0);
    }
}

TEST(test_water_blocked_by_solid) {
    World w = flat_world();
    for (int z = 4; z <= 12; ++z) w.set(10, 64, z, blk::STONE);  // a wall at x = 10
    Sim s(w);
    s.place_source(BlockPos{8, 64, 8});
    s.run(400);
    bool wall = true;
    for (int z = 4; z <= 12; ++z) wall = wall && w.get(10, 64, z) == blk::STONE;
    CHECK(wall);
    CHECK(level(w, 9, 64, 8) == 1);
    CHECK(level(w, 10, 64, 3) == 7);   // around the wall's end: 5 along z, then 2 along x
    CHECK(level(w, 11, 64, 8) == -1);  // 13 cells around the wall
    // A solid cell given a tick directly is left alone.
    s.t.schedule(BlockPos{10, 64, 8}, 0);
    fluid_update(w, s.t, BlockPos{10, 64, 8});
    CHECK(w.get(10, 64, 8) == blk::STONE);
}

TEST(test_water_stable) {
    World w = flat_world();
    w.set(11, FLOOR, 8, blk::AIR);
    w.set(11, FLOOR - 1, 8, blk::STONE);
    Sim s(w);
    s.place_source(BlockPos{8, 64, 8});
    s.place_source(BlockPos{20, 64, 20});
    s.run(400);
    CHECK(s.t.size() == 0);
    const int before = water_count(w, -16, 31, 50, 70, -16, 31);
    s.run(400);
    CHECK(water_count(w, -16, 31, 50, 70, -16, 31) == before);
}

TEST(test_water_order_independent) {
    // Placing the same sources in a different order settles to the same water.
    World a = flat_world(), b = flat_world();
    Sim sa(a), sb(b);
    sa.place_source(BlockPos{4, 64, 4});
    sa.place_source(BlockPos{10, 64, 6});
    sb.place_source(BlockPos{10, 64, 6});
    sb.place_source(BlockPos{4, 64, 4});
    sa.run(400);
    sb.run(400);
    bool same = true;
    for (int z = -16; z < 32; ++z)
        for (int x = -16; x < 32; ++x) same = same && level(a, x, 64, z) == level(b, x, 64, z);
    CHECK(same);
    CHECK(level(a, 7, 64, 5) == 4);  // 4 from each source
}

TEST(test_water_crosses_chunk_border) {
    World w = flat_world();
    Sim s(w);
    s.place_source(BlockPos{13, 64, 8});
    s.place_source(BlockPos{2, 64, 24});
    s.run(400);
    CHECK(level(w, 15, 64, 8) == 2);
    CHECK(level(w, 16, 64, 8) == 3);   // column 1
    CHECK(level(w, 0, 64, 24) == 2);
    CHECK(level(w, -1, 64, 24) == 3);  // column -1
    CHECK(level(w, 2, 64, 15) == -1);  // 9 from (2, 24)
    CHECK(level(w, 13, 64, 16) == -1); // 8 from (13, 8)
}

TEST(test_water_unloaded_neighbor) {
    World w(7);
    load_flat(w, 0, 0, 0, 0);  // only column (0, 0)
    Sim s(w);
    s.place_source(BlockPos{12, 64, 8});
    s.run(400);
    CHECK(level(w, 15, 64, 8) == 3);
    CHECK(!w.column_loaded(ColumnPos{1, 0}));
    CHECK(s.t.size() == 0);
    // The column loads; scheduling its water ticks lets the flow continue.
    load_flat(w, 1, 1, 0, 0);
    schedule_column_water(w, s.t, ColumnPos{1, 0});
    s.run(400);
    CHECK(level(w, 16, 64, 8) == 4);
    CHECK(level(w, 19, 64, 8) == 7);
    CHECK(level(w, 20, 64, 8) == -1);
}

TEST(test_water_unloaded_below_is_no_drop) {
    // An unloaded column is neither a place to flow into nor a drop to steer toward.
    World w(7);
    load_flat(w, 0, 0, 0, 0);
    Sim s(w);
    s.place_source(BlockPos{14, 64, 8});
    s.run(400);
    CHECK(level(w, 13, 64, 8) == 1);
    CHECK(level(w, 14, 64, 9) == 1);
    CHECK(level(w, 15, 64, 8) == 1);
    // A tick inside the unloaded column does nothing.
    fluid_update(w, s.t, BlockPos{16, 64, 8});
    CHECK(s.t.size() == 0);
}

TEST(test_schedule_column_water) {
    World w = flat_world();
    w.set(3, 64, 3, blk::WATER);        // a source: not scheduled
    w.set(5, 64, 5, blk::WATER, 4);     // flowing: scheduled
    w.set(-1, 64, 7, blk::WATER, 2);    // flowing in column (-1, 0), next to (0, 64, 7)
    w.set(-1, 64, 9, blk::WATER);       // a source across the border: not scheduled
    w.set(15, 64, 3, blk::WATER, 5);    // flowing on the border: it and the cell across
    TickScheduler t;
    schedule_column_water(w, t, ColumnPos{0, 0});
    const std::vector<BlockPos> v = t.due(FLUID_DELAY);
    CHECK(v.size() == 4);
    CHECK(std::find(v.begin(), v.end(), BlockPos{5, 64, 5}) != v.end());
    CHECK(std::find(v.begin(), v.end(), BlockPos{0, 64, 7}) != v.end());
    CHECK(std::find(v.begin(), v.end(), BlockPos{15, 64, 3}) != v.end());
    CHECK(std::find(v.begin(), v.end(), BlockPos{16, 64, 3}) != v.end());
    TickScheduler empty;
    schedule_column_water(w, empty, ColumnPos{9, 9});  // not loaded
    CHECK(empty.size() == 0);
}

TEST(test_water_world_edges) {
    World w(7);
    w.insert_column(ColumnPos{0, 0}, Column{});
    Sim s(w);
    // At y = 0 the cell below is DEEPSTONE: it spreads.
    s.place_source(BlockPos{8, 0, 8});
    // At y = 255 it falls.
    s.place_source(BlockPos{3, 255, 3});
    s.run(10);
    CHECK(level(w, 9, 0, 8) == 1);
    CHECK(level(w, 3, 254, 3) == 8);
    CHECK(level(w, 3, 253, 3) == 8);
    CHECK(!water(w, 4, 255, 3));
}

TEST(test_water_bad_meta) {
    World w = flat_world();
    w.set(8, 64, 8, blk::WATER, 200);  // out of range: treated as falling
    TickScheduler t;
    fluid_update(w, t, BlockPos{8, 64, 8});
    CHECK(!water(w, 8, 64, 8));  // nothing above or beside feeds it
    CHECK(t.size() == 6);        // its neighbors are told
}

TEST(test_water_far_coordinates) {
    World w(7);
    load_flat(w, 62500, 62500, -62500, -62500);  // x = 1,000,000 .. 1,000,015
    Sim s(w);
    s.place_source(BlockPos{1000008, 64, -999992});
    s.run(400);
    CHECK(level(w, 1000015, 64, -999992) == 7);
    CHECK(level(w, 1000008, 64, -999985) == 7);
    CHECK(level(w, 1000001, 64, -999992) == 7);
}

TEST(test_mesh_water_levels) {
    World w(7);
    w.insert_column(ColumnPos{0, 0}, Column{});
    w.set(2, 70, 2, blk::WATER, 3);
    w.set(6, 70, 2, blk::WATER, 0);
    w.set(10, 70, 2, blk::WATER, 8);
    w.set(2, 90, 2, blk::WATER, 7);
    w.set(6, 90, 2, blk::WATER, 1);
    w.set(10, 90, 2, blk::WATER, 3);
    w.set(10, 91, 2, blk::WATER, 0);  // water above: full height
    auto top_of = [&](int x, int y) {
        const ChunkPos p = chunk_of(x, y, 2);
        const ChunkMesh m = build_mesh(snapshot(w, p), p);
        float top = -1e9f;
        const MeshData& d = m.translucent;
        for (std::size_t i = 0; i + 2 < d.positions.size(); i += 3)
            if (d.positions[i] >= static_cast<float>(x) && d.positions[i] <= static_cast<float>(x + 1) &&
                d.positions[i + 1] < static_cast<float>(y + 1) + 0.5f && d.positions[i + 1] >= static_cast<float>(y))
                top = std::max(top, d.positions[i + 1]);
        return top;
    };
    CHECK_NEAR(top_of(2, 70), 70.0 + 5.0 / 9.0, 1e-4);
    CHECK_NEAR(top_of(6, 70), 70.875, 1e-4);
    CHECK_NEAR(top_of(10, 70), 70.875, 1e-4);
    CHECK_NEAR(top_of(2, 90), 90.0 + 1.0 / 9.0, 1e-4);
    CHECK_NEAR(top_of(6, 90), 90.0 + 7.0 / 9.0, 1e-4);
    CHECK_NEAR(top_of(10, 90), 91.0, 1e-4);
}

TEST(test_mesh_water_bad_meta_full_height) {
    World w(7);
    w.insert_column(ColumnPos{0, 0}, Column{});
    w.set(2, 70, 2, blk::WATER, 99);
    const ChunkMesh m = build_mesh(snapshot(w, ChunkPos{0, 4, 0}), ChunkPos{0, 4, 0});
    CHECK_NEAR(max_y(m.translucent), 70.875, 1e-4);
}
