// Tests for src/coords.*: constants, floor division, chunk/column positions, and hashes.
#include "coords.hpp"
#include "test.hpp"

#include <climits>
#include <cstddef>
#include <set>

TEST(test_coords_constants) {
    CHECK(CHUNK == 16);
    CHECK(WORLD_HEIGHT == 256);
    CHECK(SECTIONS == 16);
    CHECK(SEA_LEVEL == 64);
}

TEST(test_coords_negative) {
    CHECK(floor_div(-1, 16) == -1);
    CHECK(floor_mod(-1, 16) == 15);
    CHECK(floor_div(-16, 16) == -1);
    CHECK(floor_div(-17, 16) == -2);
    CHECK(floor_div(15, 16) == 0);
    CHECK(chunk_of(-1, 0, -17) == (ChunkPos{-1, 0, -2}));
}

// Exactly at, just below, and just above each multiple of the divisor, on both sides of zero.
TEST(test_coords_boundaries) {
    CHECK(floor_div(0, 16) == 0);
    CHECK(floor_mod(0, 16) == 0);
    CHECK(floor_mod(15, 16) == 15);
    CHECK(floor_div(16, 16) == 1);
    CHECK(floor_mod(16, 16) == 0);
    CHECK(floor_div(17, 16) == 1);
    CHECK(floor_mod(17, 16) == 1);
    CHECK(floor_div(-15, 16) == -1);
    CHECK(floor_mod(-15, 16) == 1);
    CHECK(floor_mod(-16, 16) == 0);
    CHECK(floor_mod(-17, 16) == 15);

    CHECK(chunk_of(0, 0, 0) == (ChunkPos{0, 0, 0}));
    CHECK(chunk_of(15, 15, 15) == (ChunkPos{0, 0, 0}));
    CHECK(chunk_of(16, 16, 16) == (ChunkPos{1, 1, 1}));
    CHECK(chunk_of(-16, 255, -16) == (ChunkPos{-1, 15, -1}));
}

// Region files use floor_div(cx, 32): the region border sits between 31 and 32.
TEST(test_coords_region_divisor) {
    CHECK(floor_div(31, 32) == 0);
    CHECK(floor_div(32, 32) == 1);
    CHECK(floor_div(-1, 32) == -1);
    CHECK(floor_div(-32, 32) == -1);
    CHECK(floor_div(-33, 32) == -2);
    CHECK(floor_mod(-33, 32) == 31);
}

TEST(test_coords_large) {
    CHECK(floor_div(1 << 30, 16) == 1 << 26);
    CHECK(floor_div(-(1 << 30) - 1, 16) == -(1 << 26) - 1);
    CHECK(floor_mod(-(1 << 30) - 1, 16) == 15);
    CHECK(chunk_of(1000000, 255, -1000000) == (ChunkPos{62500, 15, -62500}));
}

// The extremes of int must not overflow: floor_div(INT_MIN, 16) is exact.
TEST(test_coords_int_limits) {
    CHECK(floor_div(INT_MIN, 16) == -134217728);
    CHECK(floor_mod(INT_MIN, 16) == 0);
    CHECK(floor_div(INT_MAX, 16) == 134217727);
    CHECK(floor_mod(INT_MAX, 16) == 15);
    CHECK(floor_div(INT_MIN + 1, 16) == -134217728);
    CHECK(floor_mod(INT_MIN + 1, 16) == 1);
}

TEST(test_column_of) {
    CHECK(column_of(ChunkPos{-3, 7, 5}) == (ColumnPos{-3, 5}));
    CHECK(column_of(ChunkPos{0, 0, 0}) == (ColumnPos{0, 0}));
    CHECK(column_of(ChunkPos{62500, 15, -62500}) == (ColumnPos{62500, -62500}));
}

TEST(test_hash_distinct) {
    ColumnPosHash h;
    std::set<std::size_t> seen;
    for (int x = -10; x <= 10; ++x)
        for (int z = -10; z <= 10; ++z) seen.insert(h(ColumnPos{x, z}));
    CHECK(seen.size() >= 400);
}

// 11 x 16 x 11 = 1936 chunk positions; x/z swaps and y layers must not collapse together.
TEST(test_chunk_hash_distinct) {
    ChunkPosHash h;
    std::set<std::size_t> seen;
    for (int x = -5; x <= 5; ++x)
        for (int y = 0; y < 16; ++y)
            for (int z = -5; z <= 5; ++z) seen.insert(h(ChunkPos{x, y, z}));
    CHECK(seen.size() >= 1900);
}

TEST(test_hash_equal_for_equal_positions) {
    CHECK(ColumnPosHash{}(ColumnPos{-7, 3}) == ColumnPosHash{}(ColumnPos{-7, 3}));
    CHECK(ChunkPosHash{}(ChunkPos{-7, 4, 3}) == ChunkPosHash{}(ChunkPos{-7, 4, 3}));
    CHECK(ColumnPosHash{}(ColumnPos{1, 2}) != ColumnPosHash{}(ColumnPos{2, 1}));
    CHECK(ChunkPosHash{}(ChunkPos{1, 0, 2}) != ChunkPosHash{}(ChunkPos{2, 0, 1}));
    CHECK(ChunkPosHash{}(ChunkPos{0, 1, 0}) != ChunkPosHash{}(ChunkPos{0, 2, 0}));
}
