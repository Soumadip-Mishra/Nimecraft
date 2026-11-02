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
    CHECK(blk::COUNT == 9);
    CHECK(T_COUNT == 10);
    CHECK(PX == 0 && NX == 1 && PY == 2 && NY == 3 && PZ == 4 && NZ == 5);
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

// "AIR not solid/opaque; all others solid and opaque"; only DEEPSTONE is unbreakable.
TEST(test_block_flags) {
    for (BlockId id = 1; id < blk::COUNT; ++id) {
        CHECK(block_def(id).solid);
        CHECK(block_def(id).opaque);
        CHECK(block_def(id).breakable == (id != blk::DEEPSTONE));
    }
}

// Ids at and above COUNT fall back to the AIR definition; COUNT - 1 is still a real block.
TEST(test_block_def_id_boundary) {
    CHECK(&block_def(blk::COUNT) == &block_def(blk::AIR));
    CHECK(&block_def(999) == &block_def(blk::AIR));
    CHECK(&block_def(65535) == &block_def(blk::AIR));
    CHECK(&block_def(blk::COUNT - 1) == &block_def(blk::DEEPSTONE));
    CHECK(&block_def(blk::DEEPSTONE) != &block_def(blk::AIR));
    CHECK(block_def(blk::COUNT - 1).solid);
}

TEST(test_block_names_distinct) {
    for (BlockId a = 0; a < blk::COUNT; ++a)
        for (BlockId b = static_cast<BlockId>(a + 1); b < blk::COUNT; ++b)
            CHECK(block_def(a).name != block_def(b).name);
}
