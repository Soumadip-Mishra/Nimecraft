// Tests for src/growth.*: grass, saplings, the Sunroot crop, falling sand and gravel, plant drops,
// planting, and the scheduled block tick dispatch.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "growth.hpp"
#include "items.hpp"
#include "light.hpp"
#include "mesher.hpp"
#include "mining.hpp"
#include "test.hpp"
#include "ticks.hpp"
#include "world.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

// An all-AIR column at c, inserted without light (every cell 0) unless a test sets some.
void load_air(World& w, ColumnPos c) {
    w.insert_column(c, Column{});
}

// Sets the stored light of one cell directly (sky, block).
void set_light(World& w, int x, int y, int z, uint8_t sky, uint8_t block) {
    Chunk* c = w.chunk(chunk_of(x, y, z));
    const int i = Chunk::index(floor_mod(x, CHUNK), floor_mod(y, CHUNK), floor_mod(z, CHUNK));
    set_sky_light(*c, i, sky);
    set_block_light(*c, i, block);
}

void tick_n(World& w, BlockPos p, int n, uint32_t& rng) {
    for (int i = 0; i < n; ++i) random_tick_block(w, p, rng);
}

// Runs scheduled ticks through block_tick for n game ticks.
void run(World& w, TickScheduler& t, uint64_t& now, int n) {
    for (int i = 0; i < n; ++i) {
        ++now;
        for (BlockPos p : t.due(now)) block_tick(w, t, p);
    }
}

int count_of(const std::vector<ItemStack>& v, ItemId id) {
    int n = 0;
    for (const ItemStack& s : v) n += s.id == id ? s.count : 0;
    return n;
}

}  // namespace

// ---- Grass ----

TEST(test_grass_spreads_in_light) {
    for (uint8_t light : {uint8_t{9}, uint8_t{8}}) {
        World w(1);
        load_air(w, ColumnPos{0, 0});
        w.set(8, 64, 8, blk::GRASS);
        w.set(9, 64, 8, blk::DIRT);
        w.set(7, 62, 8, blk::DIRT);  // 2 blocks lower: never reached
        set_light(w, 9, 65, 8, light, 0);
        set_light(w, 7, 63, 8, 15, 0);
        uint32_t rng = 42;
        tick_n(w, BlockPos{8, 64, 8}, 2000, rng);
        CHECK(w.get(9, 64, 8) == (light == 9 ? blk::GRASS : blk::DIRT));
        CHECK(w.get(7, 62, 8) == blk::DIRT);
        CHECK(w.get(8, 64, 8) == blk::GRASS);
    }
}

TEST(test_grass_spreads_one_up_and_one_down_by_block_light) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(8, 64, 8, blk::GRASS);
    w.set(8, 65, 9, blk::DIRT);  // one up
    w.set(7, 63, 7, blk::DIRT);  // one down, diagonal
    set_light(w, 8, 66, 9, 0, 9);  // block light counts too
    set_light(w, 7, 64, 7, 0, 12);
    uint32_t rng = 7;
    tick_n(w, BlockPos{8, 64, 8}, 4000, rng);
    CHECK(w.get(8, 65, 9) == blk::GRASS);
    CHECK(w.get(7, 63, 7) == blk::GRASS);
}

TEST(test_grass_does_not_spread_under_cover) {
    // DIRT whose block above has opacity 2 or more would die at once: it is not grown into.
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(8, 64, 8, blk::GRASS);
    w.set(9, 64, 8, blk::DIRT);
    w.set(9, 65, 8, blk::WATER);
    set_light(w, 9, 65, 8, 15, 0);
    uint32_t rng = 3;
    tick_n(w, BlockPos{8, 64, 8}, 3000, rng);
    CHECK(w.get(9, 64, 8) == blk::DIRT);
}

TEST(test_grass_dies_under_block) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(2, 64, 2, blk::GRASS);
    w.set(2, 65, 2, blk::STONE);
    w.set(5, 64, 5, blk::GRASS);
    w.set(5, 65, 5, blk::LEAVES);  // opacity 1
    w.set(8, 64, 8, blk::GRASS);
    w.set(8, 65, 8, blk::WATER);   // opacity 2
    uint32_t rng = 1;
    random_tick_block(w, BlockPos{2, 64, 2}, rng);
    CHECK(w.get(2, 64, 2) == blk::DIRT);
    random_tick_block(w, BlockPos{8, 64, 8}, rng);
    CHECK(w.get(8, 64, 8) == blk::DIRT);
    tick_n(w, BlockPos{5, 64, 5}, 200, rng);
    CHECK(w.get(5, 64, 5) == blk::GRASS);
}

TEST(test_grass_world_edges_and_unloaded) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    load_air(w, ColumnPos{-1, 0});
    // GRASS at the top of the world: nothing above, it lives.
    w.set(3, 255, 3, blk::GRASS);
    uint32_t rng = 9;
    tick_n(w, BlockPos{3, 255, 3}, 50, rng);
    CHECK(w.get(3, 255, 3) == blk::GRASS);
    // Across a column border: x = -1 spreads to x = 0.
    w.set(-1, 64, 8, blk::GRASS);
    w.set(0, 64, 8, blk::DIRT);
    set_light(w, 0, 65, 8, 15, 0);
    tick_n(w, BlockPos{-1, 64, 8}, 3000, rng);
    CHECK(w.get(0, 64, 8) == blk::GRASS);
    // Toward an unloaded column: nothing happens, no crash.
    w.set(15, 64, 8, blk::GRASS);
    tick_n(w, BlockPos{15, 64, 8}, 500, rng);
    CHECK(w.get(15, 64, 8) == blk::GRASS);
    CHECK(!w.column_loaded(ColumnPos{1, 0}));
    // A tick in an unloaded column, or on a block without random ticks, does nothing.
    random_tick_block(w, BlockPos{40, 64, 8}, rng);
    w.set(4, 64, 4, blk::STONE);
    random_tick_block(w, BlockPos{4, 64, 4}, rng);
    CHECK(w.get(4, 64, 4) == blk::STONE);
}

// ---- Saplings ----

TEST(test_sapling_grows_with_space) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(8, 63, 8, blk::GRASS);
    w.set(8, 64, 8, blk::SAPLING);
    set_light(w, 8, 64, 8, 15, 0);
    uint32_t rng = 5;
    tick_n(w, BlockPos{8, 64, 8}, 10000, rng);
    // Oak, trunk 5: LOG 64..68, LEAVES 67..68 (5x5 without corners) and 69..70 (3x3).
    CHECK(w.get(8, 64, 8) == blk::LOG);
    CHECK(w.get(8, 68, 8) == blk::LOG);
    CHECK(w.get(8, 69, 8) == blk::LEAVES);
    CHECK(w.get(8, 70, 8) == blk::LEAVES);
    CHECK(w.get(8, 71, 8) == blk::AIR);
    CHECK(w.get(10, 67, 8) == blk::LEAVES);
    CHECK(w.get(10, 67, 10) == blk::AIR);  // corner left out
    CHECK(w.get(9, 70, 9) == blk::LEAVES);
    CHECK(w.get(8, 63, 8) == blk::GRASS);
}

TEST(test_pine_sapling_grows) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(4, 64, 4, blk::PINE_SAPLING);
    set_light(w, 4, 64, 4, 0, 9);  // block light 9 is enough
    uint32_t rng = 11;
    tick_n(w, BlockPos{4, 64, 4}, 10000, rng);
    // Pine trunk 6..8: PINE_LOG at 64..69 at least, a cone whose tip sits on the trunk top.
    bool trunk = true;
    for (int y = 64; y <= 69; ++y) trunk = trunk && w.get(4, y, 4) == blk::PINE_LOG;
    CHECK(trunk);
    int top = 69;
    while (w.get(4, top + 1, 4) == blk::PINE_LOG) ++top;
    CHECK(top >= 69 && top <= 71);
    CHECK(w.get(4, top + 1, 4) == blk::PINE_LEAVES);
    CHECK(w.get(6, top - 3, 4) == blk::PINE_LEAVES);  // radius 2 at the bottom layer
}

TEST(test_sapling_blocked) {
    {
        World w(1);
        load_air(w, ColumnPos{0, 0});
        w.set(8, 64, 8, blk::SAPLING);
        w.set(8, 67, 8, blk::STONE);  // 3 above: a trunk cell
        set_light(w, 8, 64, 8, 15, 0);
        uint32_t rng = 5;
        tick_n(w, BlockPos{8, 64, 8}, 10000, rng);
        CHECK(w.get(8, 64, 8) == blk::SAPLING);
        CHECK(w.get(8, 65, 8) == blk::AIR);
    }
    {
        World w(1);
        load_air(w, ColumnPos{0, 0});
        w.set(8, 64, 8, blk::SAPLING);
        set_light(w, 8, 64, 8, 8, 8);  // light 8
        uint32_t rng = 5;
        tick_n(w, BlockPos{8, 64, 8}, 10000, rng);
        CHECK(w.get(8, 64, 8) == blk::SAPLING);
    }
    {
        // A block in the canopy core (3 x 3 around the trunk axis) blocks it too.
        World w(1);
        load_air(w, ColumnPos{0, 0});
        w.set(8, 64, 8, blk::SAPLING);
        w.set(9, 69, 9, blk::PLANKS);
        set_light(w, 8, 64, 8, 15, 0);
        uint32_t rng = 5;
        tick_n(w, BlockPos{8, 64, 8}, 10000, rng);
        CHECK(w.get(8, 64, 8) == blk::SAPLING);
    }
    {
        // Outside the core the tree grows and leaves the block alone.
        World w(1);
        load_air(w, ColumnPos{0, 0});
        w.set(8, 64, 8, blk::SAPLING);
        w.set(10, 67, 8, blk::PLANKS);
        set_light(w, 8, 64, 8, 15, 0);
        uint32_t rng = 5;
        tick_n(w, BlockPos{8, 64, 8}, 10000, rng);
        CHECK(w.get(8, 64, 8) == blk::LOG);
        CHECK(w.get(10, 67, 8) == blk::PLANKS);
    }
    {
        // Too close to the top of the world for the trunk.
        World w(1);
        load_air(w, ColumnPos{0, 0});
        w.set(8, 252, 8, blk::SAPLING);
        set_light(w, 8, 252, 8, 15, 0);
        uint32_t rng = 5;
        tick_n(w, BlockPos{8, 252, 8}, 10000, rng);
        CHECK(w.get(8, 252, 8) == blk::SAPLING);
    }
}

TEST(test_sapling_canopy_in_unloaded_column) {
    // A canopy core cell in an unloaded column is not free: the sapling waits.
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(15, 64, 8, blk::SAPLING);
    set_light(w, 15, 64, 8, 15, 0);
    uint32_t rng = 5;
    tick_n(w, BlockPos{15, 64, 8}, 10000, rng);
    CHECK(w.get(15, 64, 8) == blk::SAPLING);
    load_air(w, ColumnPos{1, 0});
    tick_n(w, BlockPos{15, 64, 8}, 10000, rng);
    CHECK(w.get(15, 64, 8) == blk::LOG);
    CHECK(w.get(16, 69, 8) == blk::LEAVES);  // across the border
}

TEST(test_sapling_growth_deterministic) {
    auto grown_at = [](uint32_t seed) {
        World w(1);
        load_air(w, ColumnPos{0, 0});
        w.set(8, 64, 8, blk::SAPLING);
        set_light(w, 8, 64, 8, 15, 0);
        uint32_t rng = seed;
        for (int i = 1; i <= 10000; ++i) {
            random_tick_block(w, BlockPos{8, 64, 8}, rng);
            if (w.get(8, 64, 8) == blk::LOG) return i;
        }
        return -1;
    };
    CHECK(grown_at(77) == grown_at(77));
    CHECK(grown_at(77) > 0);
}

// ---- Sunroot crop ----

TEST(test_crop_needs_water) {
    struct Case {
        int wx, wy, wz;
        uint8_t light;
        bool grows;
    };
    const Case cases[] = {
        {12, 64, 8, 9, true},    // 4 away on the same y
        {12, 64, 12, 9, true},   // 4 away on both axes
        {13, 64, 8, 9, false},   // 5 away
        {9, 63, 8, 9, false},    // 1 below
        {9, 65, 8, 9, false},    // 1 above
        {12, 64, 8, 8, false},   // light 8
    };
    for (const Case& k : cases) {
        World w(1);
        load_air(w, ColumnPos{0, 0});
        w.set(8, 63, 8, blk::DIRT);
        w.set(8, 64, 8, blk::SUNROOT_CROP);
        w.set(k.wx, k.wy, k.wz, blk::WATER);
        set_light(w, 8, 64, 8, k.light, 0);
        uint32_t rng = 21;
        tick_n(w, BlockPos{8, 64, 8}, 1000, rng);
        CHECK(w.get(8, 64, 8) == blk::SUNROOT_CROP);
        CHECK(w.meta(8, 64, 8) == (k.grows ? 3 : 0));
        tick_n(w, BlockPos{8, 64, 8}, 1000, rng);
        CHECK(w.meta(8, 64, 8) == (k.grows ? 3 : 0));  // never past 3
    }
}

TEST(test_crop_advances_one_stage_at_a_time) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(8, 64, 8, blk::SUNROOT_CROP);
    w.set(9, 64, 8, blk::WATER);
    set_light(w, 8, 64, 8, 15, 0);
    uint32_t rng = 4;
    std::vector<int> seen;
    for (int i = 0; i < 300; ++i) {
        const int before = w.meta(8, 64, 8);
        random_tick_block(w, BlockPos{8, 64, 8}, rng);
        const int after = w.meta(8, 64, 8);
        CHECK(after == before || after == before + 1);
        if (after != before) seen.push_back(after);
    }
    CHECK(seen == (std::vector<int>{1, 2, 3}));
}

TEST(test_crop_water_across_border_and_unloaded) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    load_air(w, ColumnPos{-1, 0});
    w.set(1, 64, 8, blk::SUNROOT_CROP);
    w.set(-3, 64, 8, blk::WATER);  // 4 away, in column -1
    set_light(w, 1, 64, 8, 15, 0);
    w.set(14, 64, 8, blk::SUNROOT_CROP);  // water would have to be in unloaded column 1
    set_light(w, 14, 64, 8, 15, 0);
    uint32_t rng = 8;
    tick_n(w, BlockPos{1, 64, 8}, 1000, rng);
    tick_n(w, BlockPos{14, 64, 8}, 1000, rng);
    CHECK(w.meta(1, 64, 8) == 3);
    CHECK(w.meta(14, 64, 8) == 0);
}

TEST(test_crop_bad_meta_left_alone) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(8, 64, 8, blk::SUNROOT_CROP, 9);  // out of range: treated as ripe
    w.set(9, 64, 8, blk::WATER);
    set_light(w, 8, 64, 8, 15, 0);
    uint32_t rng = 4;
    tick_n(w, BlockPos{8, 64, 8}, 100, rng);
    CHECK(w.meta(8, 64, 8) == 9);
}

TEST(test_crop_harvest_drops) {
    bool ripe_ok = true;
    bool sunroot_seen[3] = {}, seeds_seen[4] = {};
    for (uint32_t r = 0; r < 1000; ++r) {
        const std::vector<ItemStack> d = growth_drops(blk::SUNROOT_CROP, 3, r);
        const int roots = count_of(d, itm::SUNROOT), seeds = count_of(d, itm::SUNROOT_SEEDS);
        ripe_ok = ripe_ok && roots >= 1 && roots <= 2 && seeds >= 1 && seeds <= 3 && d.size() == 2;
        if (roots >= 1 && roots <= 2) sunroot_seen[roots] = true;
        if (seeds >= 1 && seeds <= 3) seeds_seen[seeds] = true;
    }
    CHECK(ripe_ok);
    CHECK(sunroot_seen[1] && sunroot_seen[2]);
    CHECK(seeds_seen[1] && seeds_seen[2] && seeds_seen[3]);
    for (uint8_t stage : {uint8_t{0}, uint8_t{1}, uint8_t{2}}) {
        bool one_seed = true;
        for (uint32_t r = 0; r < 1000; ++r) {
            const std::vector<ItemStack> d = growth_drops(blk::SUNROOT_CROP, stage, r);
            one_seed = one_seed && d.size() == 1 && d[0].id == itm::SUNROOT_SEEDS && d[0].count == 1;
        }
        CHECK(one_seed);
    }
    // Through break_drops: Survival gets them, Creative nothing; the crop block drops nothing itself.
    ItemStack hand{};
    const std::vector<ItemStack> s = break_drops(blk::SUNROOT_CROP, nullptr, hand, GameMode::Survival, 5, 3);
    CHECK(count_of(s, itm::SUNROOT) >= 1 && count_of(s, itm::SUNROOT_SEEDS) >= 1);
    CHECK(count_of(s, blk::SUNROOT_CROP) == 0);
    const std::vector<ItemStack> young = break_drops(blk::SUNROOT_CROP, nullptr, hand, GameMode::Survival, 5, 0);
    CHECK(young.size() == 1 && young[0].id == itm::SUNROOT_SEEDS && young[0].count == 1);
    CHECK(break_drops(blk::SUNROOT_CROP, nullptr, hand, GameMode::Creative, 5, 3).empty());
}

TEST(test_growth_drops_table) {
    int sap = 0, pine = 0, seeds = 0, stone = 0;
    for (uint32_t r = 0; r < 10000; ++r) {
        sap += count_of(growth_drops(blk::LEAVES, 0, r), blk::SAPLING);
        pine += count_of(growth_drops(blk::PINE_LEAVES, 0, r), blk::PINE_SAPLING);
        seeds += count_of(growth_drops(blk::TALL_GRASS, 0, r), itm::SUNROOT_SEEDS);
        stone += static_cast<int>(growth_drops(blk::STONE, 0, r).size());
    }
    CHECK(sap >= 400 && sap <= 600);       // 5% +- 1%
    CHECK(pine >= 400 && pine <= 600);
    CHECK(seeds >= 1100 && seeds <= 1400); // 12.5% +- 1.5%
    CHECK(stone == 0);
    // The extra roll is independent of block_drop's: leaves can give a stick and a sapling at once.
    int both = 0;
    ItemStack hand{};
    for (uint32_t r = 0; r < 10000; ++r) {
        const std::vector<ItemStack> d = break_drops(blk::LEAVES, nullptr, hand, GameMode::Survival, r);
        both += count_of(d, itm::STICK) > 0 && count_of(d, blk::SAPLING) > 0 ? 1 : 0;
    }
    CHECK(both > 0);
}

// ---- Planting ----

TEST(test_plant_from) {
    CHECK(is_plant_item(itm::SUNROOT_SEEDS));
    CHECK(is_plant_item(blk::SAPLING));
    CHECK(is_plant_item(blk::PINE_SAPLING));
    CHECK(!is_plant_item(blk::STONE));
    CHECK(!is_plant_item(itm::SUNROOT));
    CHECK(plant_from(itm::SUNROOT_SEEDS, blk::GRASS) == std::optional<BlockId>{blk::SUNROOT_CROP});
    CHECK(plant_from(itm::SUNROOT_SEEDS, blk::DIRT) == std::optional<BlockId>{blk::SUNROOT_CROP});
    CHECK(!plant_from(itm::SUNROOT_SEEDS, blk::STONE));
    CHECK(!plant_from(itm::SUNROOT_SEEDS, blk::SAND));
    CHECK(plant_from(blk::SAPLING, blk::GRASS) == std::optional<BlockId>{blk::SAPLING});
    CHECK(plant_from(blk::PINE_SAPLING, blk::DIRT) == std::optional<BlockId>{blk::PINE_SAPLING});
    CHECK(!plant_from(blk::PINE_SAPLING, blk::AIR));
    CHECK(!plant_from(blk::STONE, blk::GRASS));
}

// ---- Falling blocks ----

TEST(test_sand_falls) {
    for (BlockId b : {blk::SAND, blk::GRAVEL}) {
        World w(1);
        load_air(w, ColumnPos{0, 0});
        w.set(8, 69, 8, blk::STONE);  // floor: lands at 70
        w.set(8, 80, 8, b);
        TickScheduler t;
        falling_block_update(w, t, BlockPos{8, 80, 8});
        CHECK(w.get(8, 70, 8) == b);
        CHECK(w.get(8, 80, 8) == blk::AIR);
        CHECK(t.size() > 0);  // the cells around both ends are told

        // A stack of 3 lands as 3.
        w.set(4, 69, 4, blk::STONE);
        for (int y = 80; y <= 82; ++y) w.set(4, y, 4, b);
        uint64_t now = 0;
        schedule_around(t, BlockPos{4, 79, 4}, FALL_DELAY);
        run(w, t, now, 100);
        CHECK(w.get(4, 70, 4) == b && w.get(4, 71, 4) == b && w.get(4, 72, 4) == b);
        CHECK(w.get(4, 73, 4) == blk::AIR && w.get(4, 80, 4) == blk::AIR && w.get(4, 82, 4) == blk::AIR);
    }
    // SAND on SAND does not move.
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(2, 70, 2, blk::SAND);
    w.set(2, 71, 2, blk::SAND);
    TickScheduler t;
    falling_block_update(w, t, BlockPos{2, 71, 2});
    CHECK(w.get(2, 71, 2) == blk::SAND);
    CHECK(t.size() == 0);
}

TEST(test_sand_falls_through_water_and_plants) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(8, 60, 8, blk::STONE);
    w.set(8, 61, 8, blk::WATER);
    w.set(8, 62, 8, blk::TALL_GRASS);
    w.set(8, 63, 8, blk::TORCH);
    w.set(8, 70, 8, blk::SAND);
    TickScheduler t;
    falling_block_update(w, t, BlockPos{8, 70, 8});
    CHECK(w.get(8, 61, 8) == blk::SAND);  // the lowest non-solid cell
    CHECK(w.get(8, 62, 8) == blk::TALL_GRASS);
    CHECK(w.get(8, 70, 8) == blk::AIR);
}

TEST(test_sand_falls_to_world_bottom) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    w.set(3, 10, 3, blk::SAND);
    w.set(5, 0, 5, blk::GRAVEL);
    TickScheduler t;
    falling_block_update(w, t, BlockPos{3, 10, 3});
    CHECK(w.get(3, 0, 3) == blk::SAND);
    falling_block_update(w, t, BlockPos{5, 0, 5});  // DEEPSTONE below y = 0
    CHECK(w.get(5, 0, 5) == blk::GRAVEL);
    // Other blocks and unloaded cells are left alone.
    w.set(7, 30, 7, blk::DIRT);
    falling_block_update(w, t, BlockPos{7, 30, 7});
    CHECK(w.get(7, 30, 7) == blk::DIRT);
    falling_block_update(w, t, BlockPos{40, 30, 7});
}

TEST(test_falling_block_column_border) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    load_air(w, ColumnPos{1, 0});
    load_air(w, ColumnPos{-1, 0});
    w.set(16, 69, 0, blk::STONE);
    w.set(16, 80, 0, blk::SAND);
    w.set(-1, 69, 15, blk::STONE);
    w.set(-1, 80, 15, blk::GRAVEL);
    TickScheduler t;
    falling_block_update(w, t, BlockPos{16, 80, 0});
    falling_block_update(w, t, BlockPos{-1, 80, 15});
    CHECK(w.get(16, 70, 0) == blk::SAND);
    CHECK(w.get(15, 70, 0) == blk::AIR && w.get(17, 70, 0) == blk::AIR && w.get(16, 70, -1) == blk::AIR);
    CHECK(w.get(-1, 70, 15) == blk::GRAVEL);
    CHECK(w.get(0, 70, 15) == blk::AIR && w.get(-1, 70, 16) == blk::AIR);
}

// ---- Scheduled tick dispatch ----

TEST(test_block_tick_dispatch) {
    World w(1);
    load_air(w, ColumnPos{0, 0});
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) w.set(x, 63, z, blk::STONE);
    w.set(8, 70, 8, blk::SAND);
    w.set(4, 64, 4, blk::WATER);
    TickScheduler t;
    uint64_t now = 0;
    t.schedule(BlockPos{8, 70, 8}, 1);
    t.schedule(BlockPos{5, 64, 4}, 1);  // AIR beside a source: water flows in
    run(w, t, now, 1);
    CHECK(w.get(8, 64, 8) == blk::SAND);
    CHECK(w.get(5, 64, 4) == blk::WATER && w.meta(5, 64, 4) == 1);
}

// ---- Mesh ----

TEST(test_mesh_crop_stage_tiles) {
    for (uint8_t stage = 0; stage <= 4; ++stage) {
        World w(1);
        load_air(w, ColumnPos{0, 0});
        w.set(2, 70, 2, blk::SUNROOT_CROP, stage);
        const ChunkMesh m = build_mesh(snapshot(w, ChunkPos{0, 4, 0}), ChunkPos{0, 4, 0});
        const int tile = T_SUNROOT_0 + (stage < 3 ? stage : 3);  // meta past 3 shows ripe
        float umin = 1.f, vmin = 1.f;
        for (std::size_t i = 0; i + 1 < m.cutout.texcoords.size(); i += 2) {
            umin = std::min(umin, m.cutout.texcoords[i]);
            vmin = std::min(vmin, m.cutout.texcoords[i + 1]);
        }
        CHECK(m.cutout.vertex_count() == 16);
        CHECK_NEAR(umin, (tile % 8) / 8.0, 1e-6);
        CHECK_NEAR(vmin, (tile / 8) / 8.0, 1e-6);
    }
}
