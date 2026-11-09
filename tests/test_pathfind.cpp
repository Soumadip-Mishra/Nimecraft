// Tests for src/pathfind.*: A* over walkable voxel cells (steps, drops, water, light, unloaded).
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "pathfind.hpp"
#include "test.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

namespace {

// Columns (-3..2, -3..2) with STONE at y = 0..69: the floor top is y = 70, x and z in [-48, 48).
World flat() {
    World w(1);
    for (int cx = -3; cx <= 2; ++cx)
        for (int cz = -3; cz <= 2; ++cz) {
            Column col;
            for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
            for (int y = 0; y < 70; ++y)
                for (int lz = 0; lz < CHUNK; ++lz)
                    for (int lx = 0; lx < CHUNK; ++lx)
                        col[static_cast<std::size_t>(y / CHUNK)]->blocks[static_cast<std::size_t>(
                            Chunk::index(lx, y % CHUNK, lz))] = blk::STONE;
            w.insert_column(ColumnPos{cx, cz}, std::move(col));
        }
    return w;
}

// Fills the inclusive box with b straight into the chunks: no relighting, so every cell keeps
// light 0 unless a test sets it.
void fill(World& w, int x0, int y0, int z0, int x1, int y1, int z1, BlockId b) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                w.chunk(chunk_of(x, y, z))->set(floor_mod(x, CHUNK), floor_mod(y, CHUNK), floor_mod(z, CHUNK), b);
}

void set_light(World& w, BlockPos p, uint8_t sky, uint8_t block) {
    Chunk* c = w.chunk(chunk_of(p.x, p.y, p.z));
    const int i = Chunk::index(floor_mod(p.x, CHUNK), floor_mod(p.y, CHUNK), floor_mod(p.z, CHUNK));
    set_sky_light(*c, i, sky);
    set_block_light(*c, i, block);
}

bool contains(const std::vector<BlockPos>& path, BlockPos p) {
    return std::find(path.begin(), path.end(), p) != path.end();
}

// Every step moves to one of the 8 horizontal neighbors (any height change).
bool steps_adjacent(const std::vector<BlockPos>& path) {
    for (std::size_t i = 1; i < path.size(); ++i) {
        const int dx = std::abs(path[i].x - path[i - 1].x), dz = std::abs(path[i].z - path[i - 1].z);
        if (dx > 1 || dz > 1 || dx + dz == 0) return false;
    }
    return true;
}

// The maze used by the water and light tests (feet level y = 70, walls 2 high, all else walled):
//   z=2:  D D D D D
//   z=1:  D # # # D
//   z=0:  S r r r G      S start (0,70,0), G goal (4,70,0), r the short route, D the detour.
World maze() {
    World w = flat();
    fill(w, -2, 70, -2, 6, 71, 4, blk::STONE);
    fill(w, 0, 70, 0, 4, 71, 0, blk::AIR);
    fill(w, 0, 70, 2, 4, 71, 2, blk::AIR);
    fill(w, 0, 70, 1, 0, 71, 1, blk::AIR);
    fill(w, 4, 70, 1, 4, 71, 1, blk::AIR);
    return w;
}

}  // namespace

TEST(test_path_straight) {
    const World w = flat();
    const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{10, 70, 0}, PathParams{});
    CHECK(path.has_value());
    if (!path) return;
    CHECK(path->size() == 11);
    CHECK(path->front() == (BlockPos{0, 70, 0}));
    CHECK(path->back() == (BlockPos{10, 70, 0}));
    for (std::size_t i = 0; i < path->size(); ++i) CHECK((*path)[i] == (BlockPos{static_cast<int>(i), 70, 0}));
}

TEST(test_path_diagonal_open_ground) {
    // 5 diagonal steps: 6 positions, each one along +X+Z.
    const World w = flat();
    const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{5, 70, 5}, PathParams{});
    CHECK(path.has_value() && path->size() == 6 && steps_adjacent(*path));
}

TEST(test_path_start_is_goal) {
    const World w = flat();
    const auto path = find_path(w, BlockPos{3, 70, -3}, BlockPos{3, 70, -3}, PathParams{});
    CHECK(path.has_value() && path->size() == 1 && path->front() == (BlockPos{3, 70, -3}));
}

TEST(test_path_around_wall) {
    // A wall at x = 5 across the whole loaded area, 3 high, with a gap at z = 4.
    World w = flat();
    fill(w, 5, 70, -48, 5, 72, 47, blk::STONE);
    fill(w, 5, 70, 4, 5, 72, 4, blk::AIR);
    const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{10, 70, 0}, PathParams{});
    CHECK(path.has_value());
    if (!path) return;
    CHECK(contains(*path, BlockPos{5, 70, 4}));
    CHECK(steps_adjacent(*path));
    for (const BlockPos& p : *path) CHECK(!(p.x == 5 && p.z != 4));
}

TEST(test_path_no_corner_cutting) {
    // Pillars at (1, z=0) and (0, z=1) touch only at a corner between start (0,0) and goal (1,1).
    World w = flat();
    fill(w, 1, 70, 0, 1, 72, 0, blk::STONE);
    fill(w, 0, 70, 1, 0, 72, 1, blk::STONE);
    const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{1, 70, 1}, PathParams{});
    CHECK(path.has_value());
    if (!path) return;
    CHECK(path->size() > 2);  // the direct diagonal would be 2 positions
    CHECK(steps_adjacent(*path));
    for (std::size_t i = 1; i < path->size(); ++i) {
        const BlockPos a = (*path)[i - 1], b = (*path)[i];
        if (a.x != b.x && a.z != b.z) {  // a diagonal: both sides must be free
            CHECK(w.get(b.x, a.y, a.z) == blk::AIR);
            CHECK(w.get(a.x, a.y, b.z) == blk::AIR);
        }
    }
}

TEST(test_path_step_up_one) {
    World w = flat();
    fill(w, 5, 70, -48, 47, 70, 47, blk::STONE);  // the ground is 1 higher from x = 5 on
    const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{8, 71, 0}, PathParams{});
    CHECK(path.has_value());
    if (!path) return;
    CHECK(path->back() == (BlockPos{8, 71, 0}));
    CHECK(contains(*path, BlockPos{4, 70, 0}) && contains(*path, BlockPos{5, 71, 0}));
    CHECK(steps_adjacent(*path));
}

TEST(test_path_refuses_two_high_step) {
    World w = flat();
    fill(w, 5, 70, -48, 47, 71, 47, blk::STONE);  // 2 higher from x = 5 on, across the whole area
    const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{8, 72, 0}, PathParams{});
    CHECK(!path.has_value());
}

TEST(test_path_step_up_needs_headroom) {
    // A 1-block step with a ceiling 2 above the upper ground everywhere: the step itself is fine,
    // but rising needs the cell above the mob's head free. Ceiling at y = 72 over x <= 4: the mob
    // (height 2) on y = 70 fits, yet stepping up to 71 would put its head at 72 before it moves.
    World w = flat();
    fill(w, 5, 70, -48, 47, 70, 47, blk::STONE);
    fill(w, -48, 72, -48, 4, 72, 47, blk::STONE);
    CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{8, 71, 0}, PathParams{}).has_value());
}

TEST(test_path_drop_limit) {
    {
        World w = flat();
        fill(w, -48, 70, -48, 4, 72, 47, blk::STONE);  // feet at 73 for x <= 4: a drop of 3
        const auto path = find_path(w, BlockPos{0, 73, 0}, BlockPos{8, 70, 0}, PathParams{});
        CHECK(path.has_value());
        if (path) {
            CHECK(contains(*path, BlockPos{4, 73, 0}) && contains(*path, BlockPos{5, 70, 0}));
            CHECK(steps_adjacent(*path));
        }
    }
    {
        World w = flat();
        fill(w, -48, 70, -48, 4, 73, 47, blk::STONE);  // feet at 74: a drop of 4
        CHECK(!find_path(w, BlockPos{0, 74, 0}, BlockPos{8, 70, 0}, PathParams{}).has_value());
        PathParams p;
        p.max_drop = 4;
        CHECK(find_path(w, BlockPos{0, 74, 0}, BlockPos{8, 70, 0}, p).has_value());
    }
}

TEST(test_path_height_param) {
    // A tunnel 1 high along z = 0, x = 0..10, inside solid rock: ceiling at y = 71.
    World w = flat();
    fill(w, -2, 70, -2, 12, 72, 2, blk::STONE);
    fill(w, 0, 70, 0, 10, 70, 0, blk::AIR);
    PathParams one;
    one.height = 1;
    const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{10, 70, 0}, one);
    CHECK(path.has_value() && path->size() == 11);
    CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{10, 70, 0}, PathParams{}).has_value());
}

TEST(test_path_water_cost) {
    // The short route (4 moves) has water in its 3 middle cells; the land detour takes 8 moves.
    World w = maze();
    fill(w, 1, 70, 0, 3, 70, 0, blk::WATER);
    const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{4, 70, 0}, PathParams{});
    CHECK(path.has_value());
    if (!path) return;
    CHECK(path->size() == 9);
    CHECK(contains(*path, BlockPos{2, 70, 2}));
    CHECK(!contains(*path, BlockPos{2, 70, 0}));
    // With the detour walled off, the water is walkable.
    fill(w, 2, 70, 2, 2, 71, 2, blk::STONE);
    const auto wet = find_path(w, BlockPos{0, 70, 0}, BlockPos{4, 70, 0}, PathParams{});
    CHECK(wet.has_value() && wet->size() == 5 && contains(*wet, BlockPos{2, 70, 0}));
}

TEST(test_path_deep_water_walkable) {
    // Water 3 deep over the floor: a water cell needs no solid block below it.
    World w = flat();
    fill(w, 2, 67, -48, 4, 70, 47, blk::WATER);  // a trench across the area, surface at the feet level
    const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{7, 70, 0}, PathParams{});
    CHECK(path.has_value() && contains(*path, BlockPos{3, 70, 0}));
}

TEST(test_path_goal_in_solid) {
    const World w = flat();
    CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{5, 69, 0}, PathParams{}).has_value());
    // A goal in the air (nothing below) is not a walkable cell either.
    CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{5, 75, 0}, PathParams{}).has_value());
}

TEST(test_path_unreachable_bounded) {
    World w = flat();
    fill(w, 9, 70, -1, 11, 72, 1, blk::STONE);  // a closed room around the goal (10, 70, 0)
    fill(w, 10, 70, 0, 10, 71, 0, blk::AIR);
    for (int limit : {100, 500, 2000}) {
        PathParams p;
        p.max_nodes = limit;
        int expanded = -1;
        CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{10, 70, 0}, p, &expanded).has_value());
        CHECK(expanded > 0 && expanded <= limit);
    }
    // A path that needs more expansions than allowed is not found.
    PathParams tiny;
    tiny.max_nodes = 5;
    CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{-20, 70, 0}, tiny).has_value());
    int used = -1;
    CHECK(find_path(w, BlockPos{0, 70, 0}, BlockPos{-20, 70, 0}, PathParams{}, &used).has_value());
    CHECK(used > 5 && used <= 2000);
}

TEST(test_path_bad_params) {
    const World w = flat();
    PathParams none;
    none.max_nodes = 0;
    CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{3, 70, 0}, none).has_value());
    PathParams flat_body;
    flat_body.height = 0;
    CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{3, 70, 0}, flat_body).has_value());
    PathParams no_drop;
    no_drop.max_drop = -1;  // counts as 0: no drops, level ground still fine
    CHECK(find_path(w, BlockPos{0, 70, 0}, BlockPos{3, 70, 0}, no_drop).has_value());
    // Goals outside the world's height.
    CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{3, 256, 0}, PathParams{}).has_value());
    CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{3, -1, 0}, PathParams{}).has_value());
}

TEST(test_path_unloaded) {
    const World w = flat();
    // Column 3 (x 48..63) is unloaded: World::get reads it as AIR, yet nothing there is walkable.
    CHECK(!find_path(w, BlockPos{40, 70, 0}, BlockPos{60, 70, 0}, PathParams{}).has_value());
    CHECK(!find_path(w, BlockPos{40, 70, 0}, BlockPos{-60, 70, 0}, PathParams{}).has_value());
    // Along the loaded edge is fine.
    const auto edge = find_path(w, BlockPos{47, 70, -10}, BlockPos{47, 70, 10}, PathParams{});
    CHECK(edge.has_value());
    if (edge)
        for (const BlockPos& p : *edge) CHECK(p.x <= 47);
    // An empty world.
    const World empty(1);
    CHECK(!find_path(empty, BlockPos{0, 70, 0}, BlockPos{3, 70, 0}, PathParams{}).has_value());
}

TEST(test_path_avoids_light) {
    PathParams p;
    p.avoid_light_at_or_above = 10;
    {
        World w = maze();
        set_light(w, BlockPos{2, 70, 0}, 0, 10);  // block light 10 on the short route
        const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{4, 70, 0}, p);
        CHECK(path.has_value() && !contains(*path, BlockPos{2, 70, 0}) && path->size() == 9);
        // The default (16) forbids nothing.
        const auto any = find_path(w, BlockPos{0, 70, 0}, BlockPos{4, 70, 0}, PathParams{});
        CHECK(any.has_value() && any->size() == 5);
    }
    {
        World w = maze();
        set_light(w, BlockPos{2, 70, 0}, 0, 9);
        const auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{4, 70, 0}, p);
        CHECK(path.has_value() && contains(*path, BlockPos{2, 70, 0}) && path->size() == 5);
    }
    {
        // Sky light counts at the current daylight: 15 x 0.6 = 9 is fine, 15 x 1.0 is not.
        World w = maze();
        set_light(w, BlockPos{2, 70, 0}, 15, 0);
        p.daylight = 0.6f;
        auto path = find_path(w, BlockPos{0, 70, 0}, BlockPos{4, 70, 0}, p);
        CHECK(path.has_value() && path->size() == 5);
        p.daylight = 1.f;
        path = find_path(w, BlockPos{0, 70, 0}, BlockPos{4, 70, 0}, p);
        CHECK(path.has_value() && path->size() == 9);
        // A lit goal cannot be reached.
        set_light(w, BlockPos{4, 70, 0}, 15, 0);
        CHECK(!find_path(w, BlockPos{0, 70, 0}, BlockPos{4, 70, 0}, p).has_value());
    }
}

TEST(test_cell_light) {
    World w = flat();
    set_light(w, BlockPos{-1, 70, -1}, 15, 3);
    CHECK(cell_light(w, BlockPos{-1, 70, -1}, 1.f) == 15);
    CHECK(cell_light(w, BlockPos{-1, 70, -1}, 0.15f) == 3);  // sky 15 x 0.15 = 2.25 -> 2; block 3 wins
    CHECK(cell_light(w, BlockPos{-1, 70, -1}, 0.6f) == 9);
    set_light(w, BlockPos{-1, 70, -1}, 0, 5);
    CHECK(cell_light(w, BlockPos{-1, 70, -1}, 1.f) == 5);
    CHECK(cell_light(w, BlockPos{100, 70, 0}, 1.f) == 0);  // unloaded
    CHECK(cell_light(w, BlockPos{0, 300, 0}, 1.f) == 15);  // above the world: open sky
    CHECK(cell_light(w, BlockPos{0, 300, 0}, 0.15f) == 2);
}

TEST(test_path_negative_coords) {
    World w = flat();
    fill(w, -35, 70, -45, -35, 72, -36, blk::STONE);  // a wall to walk around
    const auto a = find_path(w, BlockPos{-40, 70, -40}, BlockPos{-30, 70, -38}, PathParams{});
    CHECK(a.has_value());
    if (!a) return;
    CHECK(a->front() == (BlockPos{-40, 70, -40}) && a->back() == (BlockPos{-30, 70, -38}));
    CHECK(steps_adjacent(*a));
    for (const BlockPos& p : *a) CHECK(!(p.x == -35 && p.z >= -45 && p.z <= -36));
}

TEST(test_path_deterministic) {
    World w = flat();
    fill(w, -35, 70, -45, -35, 72, -36, blk::STONE);
    const auto a = find_path(w, BlockPos{-40, 70, -40}, BlockPos{-30, 70, -38}, PathParams{});
    const auto b = find_path(w, BlockPos{-40, 70, -40}, BlockPos{-30, 70, -38}, PathParams{});
    CHECK(a.has_value() && b.has_value());
    if (a && b) CHECK(*a == *b);
}
