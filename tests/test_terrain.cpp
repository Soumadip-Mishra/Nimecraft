// Tests for src/terrain.*: lattice hash, value noise, fBm, heights, trees, and column generation.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "terrain.hpp"
#include "test.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t SEED = 7;
constexpr int SAND_MAX = 65;  // SEA_LEVEL + 1: ground at or below this height is sand

// FNV-1a 64 over the block ids of the 4 golden columns, each id fed as 2 bytes, low byte first.
// Recorded from the first green Linux run; GCC, MinGW, and MSVC must all reproduce it unchanged.
// A mismatch on any toolchain is a bug in generation math, not a reason to change this value.
constexpr uint64_t TERRAIN_GOLDEN = 0xBA789230D8EA89E6ULL;  // M2 Task 5: biome features

ColumnPos column_at(int x, int z) {
    return ColumnPos{floor_div(x, CHUNK), floor_div(z, CHUNK)};
}

// Block at world (x, y, z); (x, z) lies inside column c and y is in [0, WORLD_HEIGHT).
BlockId block_at(const Column& col, ColumnPos c, int x, int y, int z) {
    return col[static_cast<std::size_t>(y / CHUNK)]->get(x - c.x * CHUNK, y % CHUNK, z - c.z * CHUNK);
}

bool same_column(const Column& a, const Column& b) {
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i]->blocks != b[i]->blocks) return false;
    return true;
}

// The layer rules (M1 Task 3, surface materials and water from M2 Task 2) for every (x, z) of
// column c. Heights lie in [20, 200]; y = 0 is DEEPSTONE; the top 4 blocks h-3..h are, by the
// cell's own biome: Ocean one of SAND/GRAVEL/CLAY; Desert SAND over 4 SANDSTONE; any other biome
// at h <= SEA_LEVEL + 1 SAND (beach); Snowfield SNOW over DIRT; Highlands above 140 STONE; else
// GRASS over DIRT. STONE fills the rest. Above h: WATER up to SEA_LEVEL (ICE at SEA_LEVEL in
// Snowfield), then only AIR.
void check_layers(uint32_t seed, ColumnPos c) {
    // The layers before carving, ores, and features; tests/test_caves.cpp, tests/test_ores.cpp,
    // and tests/test_features.cpp cover what those stages change.
    const Column col = generate_column(seed, c, GenOptions{false, false, false, false, false});
    for (int lz = 0; lz < CHUNK; ++lz) {
        for (int lx = 0; lx < CHUNK; ++lx) {
            const int x = c.x * CHUNK + lx, z = c.z * CHUNK + lz;
            const int h = terrain_height(seed, x, z);
            CHECK(h >= 20 && h <= 200);
            if (h < 20 || h > 200) continue;  // the reads below assume a valid height
            const Biome b = biome_at(seed, x, z);
            const BlockId top = block_at(col, c, x, h, z);
            BlockId want_top = blk::GRASS, want_under = blk::DIRT;
            int stone_top = h - 4;  // highest STONE
            if (b == Biome::Ocean) {
                CHECK(top == blk::SAND || top == blk::GRAVEL || top == blk::CLAY);
                want_top = want_under = top;
            } else if (b == Biome::Desert) {
                want_top = want_under = blk::SAND;
                stone_top = h - 8;
                for (int y = h - 7; y <= h - 4; ++y) CHECK(block_at(col, c, x, y, z) == blk::SANDSTONE);
            } else if (h <= SAND_MAX) {
                want_top = want_under = blk::SAND;
            } else if (b == Biome::Snowfield) {
                want_top = blk::SNOW;
            } else if (b == Biome::Highlands && h > 140) {
                want_top = want_under = blk::STONE;
            }
            CHECK(block_at(col, c, x, 0, z) == blk::DEEPSTONE);
            CHECK(top == want_top);
            bool stone = true, under_top = true, water = true, above = true;
            for (int y = 1; y <= stone_top; ++y) stone = stone && block_at(col, c, x, y, z) == blk::STONE;
            for (int y = h - 3; y <= h - 1; ++y) under_top = under_top && block_at(col, c, x, y, z) == want_under;
            for (int y = h + 1; y <= SEA_LEVEL; ++y) {
                const BlockId w = (y == SEA_LEVEL && b == Biome::Snowfield) ? blk::ICE : blk::WATER;
                water = water && block_at(col, c, x, y, z) == w;
            }
            for (int y = std::max(h, SEA_LEVEL) + 1; y < WORLD_HEIGHT; ++y) {
                const BlockId a = block_at(col, c, x, y, z);
                above = above && a == blk::AIR;
            }
            CHECK(stone);
            CHECK(under_top);
            CHECK(water);
            CHECK(above);
        }
    }
}

// M2 Task 5 replaced M1's tree_at with features_near; the M1 tree tests below find oaks there.
// No tree other than the one at (ox, oz) lies within d blocks of (x, z) in `near`, and no plant
// or cactus stands on (x, z), so no other feature can put a block at (x, z).
bool only_origin_near(const std::vector<Feature>& near, int x, int z, int ox, int oz, int d = 2) {
    for (const Feature& f : near) {
        if (f.x == ox && f.z == oz) continue;
        const bool tree = f.type == FeatureType::OakTree || f.type == FeatureType::PineTree;
        if (tree ? (std::abs(f.x - x) <= d && std::abs(f.z - z) <= d) : (f.x == x && f.z == z)) return false;
    }
    return true;
}

uint64_t fnv1a_column(uint64_t h, const Column& col) {
    for (const auto& chunk : col) {
        for (BlockId b : chunk->blocks) {
            h ^= static_cast<uint64_t>(b & 0xFFu);
            h *= 0x100000001B3ULL;
            h ^= static_cast<uint64_t>(b >> 8);
            h *= 0x100000001B3ULL;
        }
    }
    return h;
}

}  // namespace

// ---- hash and noise ----

TEST(test_hash2_mixing) {
    CHECK(hash2(SEED, 3, 4) == hash2(SEED, 3, 4));
    CHECK(hash2(SEED, 3, 4) != hash2(SEED + 1, 3, 4));
    CHECK(hash2(SEED, 3, 4) != hash2(SEED, 4, 4));
    CHECK(hash2(SEED, 3, 4) != hash2(SEED, 3, 5));
    CHECK(hash2(SEED, 3, 4) != hash2(SEED, 4, 3));
    CHECK(hash2(SEED, -3, 4) != hash2(SEED, 3, 4));
    CHECK(hash2(SEED, 3, -4) != hash2(SEED, 3, 4));
    // Extreme inputs wrap instead of overflowing (UBSan aborts on signed overflow).
    CHECK(hash2(0xFFFFFFFFu, INT_MIN, INT_MAX) != hash2(0xFFFFFFFFu, INT_MAX, INT_MIN));
    CHECK(hash2(0u, INT_MIN, INT_MIN) != hash2(0u, INT_MAX, INT_MAX));
    // Low bits pick features (% 64 here): over 65,536 cells about 1,024 hit (binomial sd ~32).
    int hits = 0;
    for (int z = -128; z < 128; ++z)
        for (int x = -128; x < 128; ++x)
            if (hash2(SEED ^ 0x7EE5u, x, z) % 64 == 0) ++hits;
    CHECK(hits >= 900 && hits <= 1150);
}

TEST(test_noise_range_and_determinism) {
    bool in_range = true, repeatable = true, seeds_differ = false;
    for (int i = 0; i < 10000; ++i) {
        const float x = -50.0f + static_cast<float>(i % 100) * 1.013f;
        const float z = -50.0f + static_cast<float>(i / 100) * 1.027f;
        const float v = fbm2(1, x, z, 5);
        in_range = in_range && v >= 0.0f && v <= 1.0f;
        repeatable = repeatable && fbm2(1, x, z, 5) == v;
        seeds_differ = seeds_differ || fbm2(2, x, z, 5) != v;
        const float n = value_noise2(1, x, z);
        in_range = in_range && n >= 0.0f && n <= 1.0f;
    }
    CHECK(in_range);
    CHECK(repeatable);
    CHECK(seeds_differ);
}

TEST(test_noise_continuity) {
    // Samples cross many lattice lines, including negative ones, in both directions.
    bool smooth_x = true, smooth_z = true;
    for (int i = 0; i < 1000; ++i) {
        const float x = -20.0f + static_cast<float>(i) * 0.0413f;
        const float z = 3.3f - static_cast<float>(i) * 0.0177f;
        const float v = value_noise2(3, x, z);
        smooth_x = smooth_x && std::fabs(v - value_noise2(3, x + 0.01f, z)) < 0.05f;
        smooth_z = smooth_z && std::fabs(v - value_noise2(3, x, z + 0.01f)) < 0.05f;
    }
    CHECK(smooth_x);
    CHECK(smooth_z);
}

TEST(test_value_noise_spread) {
    // At lattice points the noise equals the uniform lattice values, so 10,000 of them reach
    // both ends of [0,1].
    float lo = 1.0f, hi = 0.0f;
    for (int j = -50; j < 50; ++j) {
        for (int i = -50; i < 50; ++i) {
            const float v = value_noise2(5, static_cast<float>(i), static_cast<float>(j));
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    CHECK(lo < 0.01f);
    CHECK(hi > 0.99f);
}

TEST(test_fbm_gain_lacunarity) {
    constexpr uint32_t STEP = 0x9E3779B9u;  // octave i uses seed + i * STEP (terrain.hpp)
    for (int i = 0; i < 200; ++i) {
        const float x = -7.5f + static_cast<float>(i) * 0.173f;
        const float z = 11.25f - static_cast<float>(i) * 0.091f;
        const float o0 = value_noise2(SEED, x, z);
        const float o1 = value_noise2(SEED + STEP, x * 2.0f, z * 2.0f);
        const float o2 = value_noise2(SEED + 2u * STEP, x * 4.0f, z * 4.0f);
        CHECK(fbm2(SEED, x, z, 1) == o0);
        CHECK_NEAR(fbm2(SEED, x, z, 2), (o0 + 0.5f * o1) / 1.5f, 1e-6);
        CHECK_NEAR(fbm2(SEED, x, z, 3), (o0 + 0.5f * o1 + 0.25f * o2) / 1.75f, 1e-6);
    }
}

// ---- heights and trees ----

// M2 Task 2 replaced the M1 formula with the biome blend; test_biome_height_uniform_formula
// checks the new formula. Here: the same positions, including x = +-1,000,000, give heights in
// [20, 200] that repeat exactly.
TEST(test_terrain_height_formula) {
    const std::array<int, 7> xs{-1000000, -97, -1, 0, 1, 95, 1000000};
    for (int x : xs) {
        for (int z = -300; z < 300; z += 7) {
            const int h = terrain_height(SEED, x, z);
            CHECK(h >= 20 && h <= 200);
            CHECK(terrain_height(SEED, x, z) == h);
            CHECK(terrain_height(SEED, z, x) >= 20 && terrain_height(SEED, z, x) <= 200);
        }
    }
}

TEST(test_oak_beach_boundary) {
    // M1's tree_at rule moved into features_near (M2 Task 5; tests/test_features.cpp checks every
    // rule): a Forest oak hit, hash2(seed ^ 0x7EE5, x, z) % 12 == 0, is an oak on GRASS at h = 66
    // and nothing on beach SAND at h = 65.
    bool saw65 = false, saw66 = false;
    for (int z = -1024; z < 1024 && !(saw65 && saw66); ++z)
        for (int x = -1024; x < 1024 && !(saw65 && saw66); ++x) {
            if (hash2(SEED ^ 0x7EE5u, x, z) % 12 != 0 || biome_at(SEED, x, z) != Biome::Forest) continue;
            const int h = terrain_height(SEED, x, z);
            if (!(h == 65 && !saw65) && !(h == 66 && !saw66)) continue;
            bool oak = false;
            for (const Feature& f : features_near(SEED, column_at(x, z), 0))
                oak = oak || (f.x == x && f.z == z && f.type == FeatureType::OakTree && f.y == h + 1);
            if (h == 65) {
                saw65 = true;
                CHECK(!oak);
            } else {
                saw66 = true;
                CHECK(oak);
            }
        }
    CHECK(saw65);
    CHECK(saw66);
}

// ---- columns ----

TEST(test_column_layers) {
    check_layers(SEED, ColumnPos{0, 0});
    check_layers(SEED, ColumnPos{-3, 5});
}

TEST(test_column_layers_far_and_negative) {
    check_layers(SEED, ColumnPos{-1, -1});
    check_layers(SEED, ColumnPos{62500, -62500});  // block x = 1,000,000
    check_layers(SEED, ColumnPos{-62500, 62500});
}

// The beach rule at its boundary, in the GRASS biomes (M2 Task 2 gives Desert, Snowfield, Ocean,
// and high Highlands their own surfaces).
TEST(test_sand_grass_boundary) {
    bool found65 = false, found66 = false;
    for (int z = 0; z < 4096 && !(found65 && found66); ++z) {
        for (int x = 0; x < 1024 && !(found65 && found66); ++x) {
            const int h = terrain_height(SEED, x, z);
            if (h != 65 && h != 66) continue;
            const Biome b = biome_at(SEED, x, z);
            if (b != Biome::Meadow && b != Biome::Forest && b != Biome::Taiga && b != Biome::Highlands) continue;
            if (h == 65 && !found65) {
                found65 = true;
                const ColumnPos c = column_at(x, z);
                const Column col = generate_column(SEED, c);
                CHECK(block_at(col, c, x, 61, z) == blk::STONE);
                for (int y = 62; y <= 65; ++y) CHECK(block_at(col, c, x, y, z) == blk::SAND);
                CHECK(block_at(col, c, x, 66, z) == blk::AIR);
            } else if (h == 66 && !found66) {
                found66 = true;
                const ColumnPos c = column_at(x, z);
                const Column col = generate_column(SEED, c);
                CHECK(block_at(col, c, x, 62, z) == blk::STONE);
                for (int y = 63; y <= 65; ++y) CHECK(block_at(col, c, x, y, z) == blk::DIRT);
                CHECK(block_at(col, c, x, 66, z) == blk::GRASS);
                const BlockId above = block_at(col, c, x, 67, z);  // M2 Task 5 adds pines and plants
                CHECK(above == blk::AIR || above == blk::LOG || above == blk::PINE_LOG || above == blk::TALL_GRASS ||
                      above == blk::FLOWER);
            }
        }
    }
    CHECK(found65);
    CHECK(found66);
}

TEST(test_generate_deterministic) {
    const Column a = generate_column(SEED, ColumnPos{2, -1});
    const Column b = generate_column(SEED, ColumnPos{2, -1});
    CHECK(same_column(a, b));
}

TEST(test_column_fully_defined) {
    const std::array<ColumnPos, 3> cols{{{0, 0}, {-1, -1}, {62500, -62500}}};
    for (ColumnPos c : cols) {
        const Column col = generate_column(SEED, c);
        for (int y = 0; y < SECTIONS; ++y) {
            const Chunk* ch = col[static_cast<std::size_t>(y)].get();
            CHECK(ch != nullptr);
            if (ch == nullptr) continue;
            CHECK(ch->pos == (ChunkPos{c.x, y, c.z}));
            CHECK(!ch->modified);
            CHECK(std::all_of(ch->blocks.begin(), ch->blocks.end(),
                              [](BlockId b) { return b < blk::COUNT; }));
        }
    }
}

TEST(test_seed_extremes) {
    check_layers(0u, ColumnPos{0, 0});
    check_layers(0xFFFFFFFFu, ColumnPos{0, 0});
    CHECK(!same_column(generate_column(0u, ColumnPos{0, 0}), generate_column(0xFFFFFFFFu, ColumnPos{0, 0})));
}

// M1 used columns (0..3, 0..3), which M2 Task 2 turned into deep Ocean for seed 7; the 4x4
// columns at the first Forest point of a 64-block grid are land.
TEST(test_trees_exist) {
    int fx = 0, fz = 0;
    bool forest = false;
    for (int z = 0; z < 8192 && !forest; z += 64)
        for (int x = 0; x < 8192 && !forest; x += 64)
            if (biome_at(SEED, x, z) == Biome::Forest && terrain_height(SEED, x, z) > SAND_MAX) {
                forest = true;
                fx = x;
                fz = z;
            }
    CHECK(forest);
    const ColumnPos origin = column_at(fx, fz);
    int logs = 0;
    for (int cz = origin.z; cz < origin.z + 4; ++cz) {
        for (int cx = origin.x; cx < origin.x + 4; ++cx) {
            const Column col = generate_column(SEED, ColumnPos{cx, cz});
            for (const auto& ch : col) logs += static_cast<int>(std::count(ch->blocks.begin(), ch->blocks.end(), blk::LOG));
        }
    }
    CHECK(logs >= 1);
}

TEST(test_tree_shape) {
    // An oak with no other tree within 5 blocks, on ground no higher than h+2 within 3 blocks (so
    // plants there stay below h+4), with its 7x7 surroundings inside one column, so every cell
    // checked is exact.
    bool found = false;
    for (int cz = -64; cz < 64 && !found; ++cz) {
        for (int cx = -64; cx < 64 && !found; ++cx) {
            const Biome cb = biome_at(SEED, cx * CHUNK + 8, cz * CHUNK + 8);
            if (cb != Biome::Meadow && cb != Biome::Highlands && cb != Biome::Forest) continue;
            const ColumnPos c{cx, cz};
            const std::vector<Feature> near = features_near(SEED, c, 3);
            for (const Feature& f : near) {
                const int lx = f.x - cx * CHUNK, lz = f.z - cz * CHUNK;
                if (f.type != FeatureType::OakTree || lx < 3 || lx > 12 || lz < 3 || lz > 12) continue;
                const int x = f.x, z = f.z, h = f.y - 1;
                bool clear = true;
                for (const Feature& o : near) {
                    const bool tree = o.type == FeatureType::OakTree || o.type == FeatureType::PineTree;
                    if (tree && (o.x != x || o.z != z) && std::abs(o.x - x) <= 5 && std::abs(o.z - z) <= 5) clear = false;
                }
                for (int dz = -3; dz <= 3 && clear; ++dz)
                    for (int dx = -3; dx <= 3 && clear; ++dx)
                        if (terrain_height(SEED, x + dx, z + dz) > h + 2) clear = false;
                if (!clear) continue;
                found = true;

                const Column col = generate_column(SEED, c);
                CHECK(block_at(col, c, x, h, z) == blk::GRASS);
                for (int y = h + 1; y <= h + 5; ++y) CHECK(block_at(col, c, x, y, z) == blk::LOG);
                CHECK(block_at(col, c, x, h + 6, z) == blk::LEAVES);
                CHECK(block_at(col, c, x, h + 7, z) == blk::LEAVES);
                for (int dz = -3; dz <= 3; ++dz) {
                    for (int dx = -3; dx <= 3; ++dx) {
                        const int ax = std::abs(dx), az = std::abs(dz);
                        const bool centre = dx == 0 && dz == 0;
                        const bool wide = ax <= 2 && az <= 2 && !(ax == 2 && az == 2);
                        const bool narrow = ax <= 1 && az <= 1;
                        if (!centre) {
                            for (int y = h + 1; y <= h + 3; ++y) {
                                const BlockId b = block_at(col, c, x + dx, y, z + dz);
                                CHECK(b != blk::LEAVES && b != blk::LOG);
                            }
                            for (int y = h + 4; y <= h + 5; ++y)
                                CHECK(block_at(col, c, x + dx, y, z + dz) == (wide ? blk::LEAVES : blk::AIR));
                            for (int y = h + 6; y <= h + 7; ++y)
                                CHECK(block_at(col, c, x + dx, y, z + dz) == (narrow ? blk::LEAVES : blk::AIR));
                        }
                        CHECK(block_at(col, c, x + dx, h + 8, z + dz) == blk::AIR);
                    }
                }
                break;
            }
        }
    }
    CHECK(found);
}

TEST(test_tree_canopy_crosses_border) {
    // The first oak at x = 15 whose canopy cells at x = 16 and 17 no other feature can reach and
    // no ground fills.
    bool found = false;
    int tz = 0, h = 0;
    for (int cz = 0; cz < 256 && !found; ++cz) {
        const std::vector<Feature> near = features_near(SEED, ColumnPos{0, cz}, 3);
        for (const Feature& f : near) {
            if (f.type != FeatureType::OakTree || f.x != 15 || floor_div(f.z, CHUNK) != cz) continue;
            if (!only_origin_near(near, 16, f.z, 15, f.z) || !only_origin_near(near, 17, f.z, 15, f.z)) continue;
            if (terrain_height(SEED, 16, f.z) >= f.y + 3 || terrain_height(SEED, 17, f.z) >= f.y + 3) continue;
            found = true;
            tz = f.z;
            h = f.y - 1;
            break;
        }
    }
    CHECK(found);
    if (!found) return;
    const ColumnPos west{0, floor_div(tz, CHUNK)}, east{1, floor_div(tz, CHUNK)};

    const Column east_alone = generate_column(SEED, east);
    CHECK(block_at(east_alone, east, 17, h + 4, tz) == blk::LEAVES);
    CHECK(block_at(east_alone, east, 16, h + 4, tz) == blk::LEAVES);
    const Column west_alone = generate_column(SEED, west);
    for (int y = h + 1; y <= h + 5; ++y) CHECK(block_at(west_alone, west, 15, y, tz) == blk::LOG);

    {
        const Column w = generate_column(SEED, west);
        const Column e = generate_column(SEED, east);
        CHECK(same_column(e, east_alone));
        CHECK(same_column(w, west_alone));
    }
    {
        const Column e = generate_column(SEED, east);
        const Column w = generate_column(SEED, west);
        CHECK(same_column(e, east_alone));
        CHECK(same_column(w, west_alone));
    }
}

TEST(test_tree_canopy_crosses_negative_z_border) {
    // Oak origin at z = -16 (first row of column z = -1); leaves reach z = -17 and -18 in column -2.
    bool found = false;
    for (int cx = -1; cx >= -256 && !found; --cx) {
        const std::vector<Feature> near = features_near(SEED, ColumnPos{cx, -1}, 3);
        for (const Feature& f : near) {
            if (f.type != FeatureType::OakTree || f.z != -16 || floor_div(f.x, CHUNK) != cx) continue;
            const int x = f.x, h = f.y - 1;
            if (!only_origin_near(near, x, -17, x, -16) || !only_origin_near(near, x, -18, x, -16) ||
                terrain_height(SEED, x, -17) > h + 3 || terrain_height(SEED, x, -18) > h + 3)
                continue;  // that cell could hold something else instead of leaves
            found = true;
            const ColumnPos south = column_at(x, -18);
            CHECK(south.z == -2);
            const Column col = generate_column(SEED, south);
            CHECK(block_at(col, south, x, h + 4, -17) == blk::LEAVES);
            CHECK(block_at(col, south, x, h + 5, -18) == blk::LEAVES);
            CHECK(block_at(col, south, x, h + 6, -17) == blk::LEAVES);
            break;
        }
    }
    CHECK(found);
}

TEST(test_tree_canopy_reaches_two_outside) {
    // An oak exactly 2 blocks outside a column still puts leaves into it, on all four sides:
    // x = 14 reaches x = 16 (column 1), x = 17 reaches x = 15 (column 0), and the same along z.
    // Nothing else can put a block on the target cell.
    struct Case {
        int origin, target, column;
        bool along_x;
    };
    const std::array<Case, 4> cases{{{14, 16, 1, true}, {17, 15, 0, true}, {14, 16, 1, false}, {17, 15, 0, false}}};
    for (const Case& k : cases) {
        bool found = false;
        for (int s = 0; s < 256 && !found; ++s) {
            // The origin's column: tested axis fixed at k.origin, the other axis at column s.
            const ColumnPos oc = k.along_x ? column_at(k.origin, s * CHUNK) : column_at(s * CHUNK, k.origin);
            const std::vector<Feature> near = features_near(SEED, oc, 3);
            for (const Feature& f : near) {
                if (f.type != FeatureType::OakTree || column_at(f.x, f.z) != oc) continue;
                if ((k.along_x ? f.x : f.z) != k.origin) continue;
                const int tx = k.along_x ? k.target : f.x, tz = k.along_x ? f.z : k.target;
                const int h = f.y - 1;
                if (terrain_height(SEED, tx, tz) > h + 3 || !only_origin_near(near, tx, tz, f.x, f.z)) continue;
                found = true;
                const ColumnPos c = column_at(tx, tz);
                CHECK((k.along_x ? c.x : c.z) == k.column);
                const Column col = generate_column(SEED, c);
                CHECK(block_at(col, c, tx, h + 4, tz) == blk::LEAVES);
                CHECK(block_at(col, c, tx, h + 5, tz) == blk::LEAVES);
                break;
            }
        }
        CHECK(found);
    }
}

TEST(test_generate_concurrent) {
    constexpr std::size_t N = 8;
    const std::array<ColumnPos, N> cols{{{0, 0}, {1, 0}, {-1, 2}, {3, -4}, {-7, -7}, {12, 5}, {0, -9}, {62500, -62500}}};
    std::array<Column, N> serial, parallel;
    for (std::size_t i = 0; i < N; ++i) serial[i] = generate_column(SEED, cols[i]);
    std::vector<std::thread> workers;
    for (std::size_t t = 0; t < 4; ++t) {
        workers.emplace_back([&cols, &parallel, t] {
            for (std::size_t i = t; i < N; i += 4) parallel[i] = generate_column(SEED, cols[i]);
        });
    }
    for (auto& w : workers) w.join();
    for (std::size_t i = 0; i < N; ++i) CHECK(same_column(serial[i], parallel[i]));
}

TEST(test_terrain_golden_hash) {
    uint64_t h = 0xCBF29CE484222325ULL;
    const std::array<ColumnPos, 4> cols{{{0, 0}, {-5, 3}, {40, -17}, {-1000, 999}}};
    for (ColumnPos c : cols) h = fnv1a_column(h, generate_column(7, c));
    if (h != TERRAIN_GOLDEN) std::printf("  terrain hash = 0x%016" PRIX64 "\n", h);
    CHECK(h == TERRAIN_GOLDEN);
}
