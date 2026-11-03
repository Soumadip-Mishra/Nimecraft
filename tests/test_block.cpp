// Tests for src/block.*: block ids, the BlockDef table, and face tiles.
#include "block.hpp"
#include "test.hpp"

#include <array>

namespace {

bool all_tiles(const BlockDef& d, Tile t) {
    for (Tile x : d.tiles)
        if (x != t) return false;
    return true;
}

}  // namespace

TEST(test_block_ids) {
    CHECK(blk::AIR == 0);
    CHECK(blk::GRASS == 1);
    CHECK(blk::DIRT == 2);
    CHECK(blk::STONE == 3);
    CHECK(blk::SAND == 4);
    CHECK(blk::LOG == 5);
    CHECK(blk::LEAVES == 6);
    CHECK(blk::PLANKS == 7);
    CHECK(blk::DEEPSTONE == 8);
    CHECK(blk::COUNT == 26);
    CHECK(T_COUNT == 31);
    CHECK(PX == 0 && NX == 1 && PY == 2 && NY == 3 && PZ == 4 && NZ == 5);
}

// M2 README: the appended ids.
TEST(test_block_ids_m2) {
    CHECK(blk::WATER == 9);
    CHECK(blk::GRAVEL == 10);
    CHECK(blk::CLAY == 11);
    CHECK(blk::SNOW == 12);
    CHECK(blk::ICE == 13);
    CHECK(blk::SANDSTONE == 14);
    CHECK(blk::CACTUS == 15);
    CHECK(blk::PINE_LOG == 16);
    CHECK(blk::PINE_LEAVES == 17);
    CHECK(blk::TALL_GRASS == 18);
    CHECK(blk::FLOWER == 19);
    CHECK(blk::COAL_ORE == 20);
    CHECK(blk::COPPER_ORE == 21);
    CHECK(blk::TIN_ORE == 22);
    CHECK(blk::IRON_ORE == 23);
    CHECK(blk::SILVER_ORE == 24);
    CHECK(blk::QUARTZ == 25);
}

// M2 Task 1: LEAVES and PINE_LEAVES are Cutout, WATER and ICE Translucent, TALL_GRASS and FLOWER
// Cross plants in Cutout, WATER a Liquid; every other block is an Opaque Cube.
TEST(test_block_layers_shapes) {
    for (BlockId id = 1; id < blk::COUNT; ++id) {
        const BlockDef& d = block_def(id);
        const bool cutout = id == blk::LEAVES || id == blk::PINE_LEAVES || id == blk::TALL_GRASS ||
                            id == blk::FLOWER;
        const bool translucent = id == blk::WATER || id == blk::ICE;
        if (cutout) CHECK(d.layer == Layer::Cutout);
        else if (translucent) CHECK(d.layer == Layer::Translucent);
        else CHECK(d.layer == Layer::Opaque);
        if (id == blk::TALL_GRASS || id == blk::FLOWER) CHECK(d.shape == Shape::Cross);
        else if (id == blk::WATER) CHECK(d.shape == Shape::Liquid);
        else CHECK(d.shape == Shape::Cube);
        // Only Opaque-layer blocks hide their neighbors' faces.
        CHECK(d.opaque == (d.layer == Layer::Opaque));
    }
}

TEST(test_block_defs_m2_tiles) {
    const BlockDef& snow = block_def(blk::SNOW);
    CHECK(snow.tiles[PY] == T_SNOW);
    CHECK(snow.tiles[NY] == T_DIRT);
    CHECK(snow.tiles[PX] == T_SNOW_SIDE);
    CHECK(snow.tiles[NZ] == T_SNOW_SIDE);
    const BlockDef& sandstone = block_def(blk::SANDSTONE);
    CHECK(sandstone.tiles[PY] == T_SANDSTONE_TOP);
    CHECK(sandstone.tiles[NY] == T_SANDSTONE_TOP);
    CHECK(sandstone.tiles[PZ] == T_SANDSTONE_SIDE);
    const BlockDef& cactus = block_def(blk::CACTUS);
    CHECK(cactus.tiles[PY] == T_CACTUS_TOP);
    CHECK(cactus.tiles[NX] == T_CACTUS_SIDE);
    const BlockDef& pine = block_def(blk::PINE_LOG);
    CHECK(pine.tiles[PY] == T_PINE_LOG_TOP);
    CHECK(pine.tiles[NY] == T_PINE_LOG_TOP);
    CHECK(pine.tiles[PX] == T_PINE_LOG_SIDE);
    CHECK(all_tiles(block_def(blk::WATER), T_WATER));
    CHECK(all_tiles(block_def(blk::GRAVEL), T_GRAVEL));
    CHECK(all_tiles(block_def(blk::CLAY), T_CLAY));
    CHECK(all_tiles(block_def(blk::ICE), T_ICE));
    CHECK(all_tiles(block_def(blk::PINE_LEAVES), T_PINE_LEAVES));
    CHECK(all_tiles(block_def(blk::TALL_GRASS), T_TALL_GRASS));
    CHECK(all_tiles(block_def(blk::FLOWER), T_FLOWER));
    CHECK(all_tiles(block_def(blk::COAL_ORE), T_COAL_ORE));
    CHECK(all_tiles(block_def(blk::COPPER_ORE), T_COPPER_ORE));
    CHECK(all_tiles(block_def(blk::TIN_ORE), T_TIN_ORE));
    CHECK(all_tiles(block_def(blk::IRON_ORE), T_IRON_ORE));
    CHECK(all_tiles(block_def(blk::SILVER_ORE), T_SILVER_ORE));
    CHECK(all_tiles(block_def(blk::QUARTZ), T_QUARTZ));
}

TEST(test_block_defs) {
    const BlockDef& grass = block_def(blk::GRASS);
    CHECK(grass.tiles[PY] == T_GRASS_TOP);
    CHECK(grass.tiles[NY] == T_DIRT);
    CHECK(grass.tiles[PX] == T_GRASS_SIDE);
    CHECK(grass.tiles[NX] == T_GRASS_SIDE);
    CHECK(grass.tiles[PZ] == T_GRASS_SIDE);
    CHECK(grass.tiles[NZ] == T_GRASS_SIDE);

    const BlockDef& log = block_def(blk::LOG);
    CHECK(log.tiles[PY] == T_LOG_TOP);
    CHECK(log.tiles[NY] == T_LOG_TOP);
    CHECK(log.tiles[PX] == T_LOG_SIDE);
    CHECK(log.tiles[NX] == T_LOG_SIDE);
    CHECK(log.tiles[PZ] == T_LOG_SIDE);
    CHECK(log.tiles[NZ] == T_LOG_SIDE);

    CHECK(all_tiles(block_def(blk::DIRT), T_DIRT));
    CHECK(all_tiles(block_def(blk::STONE), T_STONE));
    CHECK(all_tiles(block_def(blk::SAND), T_SAND));
    CHECK(all_tiles(block_def(blk::LEAVES), T_LEAVES));
    CHECK(all_tiles(block_def(blk::PLANKS), T_PLANKS));
    CHECK(all_tiles(block_def(blk::DEEPSTONE), T_DEEPSTONE));

    CHECK(!block_def(blk::DEEPSTONE).breakable);
    CHECK(block_def(999).solid == false);
}

TEST(test_block_defs_complete) {
    for (BlockId id = 0; id < blk::COUNT; ++id) {
        const BlockDef& d = block_def(id);
        CHECK(!d.name.empty());
        CHECK(d.tiles.size() == 6);
        for (Tile t : d.tiles) CHECK(t < T_COUNT);
        if (d.opaque) CHECK(d.solid);
    }
    CHECK(!block_def(blk::AIR).solid);
    CHECK(!block_def(blk::AIR).opaque);
}

// M1: "AIR not solid/opaque; all others solid and opaque". M2 adds blocks that are walked through
// (WATER, TALL_GRASS, FLOWER) and blocks that are seen through (LEAVES, PINE_LEAVES, ICE, and the
// walk-through ones). DEEPSTONE and WATER are the unbreakable ones.
TEST(test_block_flags) {
    for (BlockId id = 1; id < blk::COUNT; ++id) {
        const bool walk_through = id == blk::WATER || id == blk::TALL_GRASS || id == blk::FLOWER;
        const bool see_through =
            walk_through || id == blk::LEAVES || id == blk::PINE_LEAVES || id == blk::ICE;
        CHECK(block_def(id).solid == !walk_through);
        CHECK(block_def(id).opaque == !see_through);
        CHECK(block_def(id).breakable == (id != blk::DEEPSTONE && id != blk::WATER));
    }
}

// Ids at and above COUNT fall back to the AIR definition; COUNT - 1 is still a real block.
TEST(test_block_def_id_boundary) {
    CHECK(&block_def(blk::COUNT) == &block_def(blk::AIR));
    CHECK(&block_def(999) == &block_def(blk::AIR));
    CHECK(&block_def(65535) == &block_def(blk::AIR));
    CHECK(&block_def(blk::COUNT - 1) == &block_def(blk::QUARTZ));
    CHECK(&block_def(blk::QUARTZ) != &block_def(blk::AIR));
    CHECK(block_def(blk::COUNT - 1).solid);
}

TEST(test_block_names_distinct) {
    for (BlockId a = 0; a < blk::COUNT; ++a)
        for (BlockId b = static_cast<BlockId>(a + 1); b < blk::COUNT; ++b)
            CHECK(block_def(a).name != block_def(b).name);
}
