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
constexpr uint64_t TERRAIN_GOLDEN = 0xEDE211C035EACBB0ULL;

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

// The M1 layer rules for every (x, z) of column c.
void check_layers(uint32_t seed, ColumnPos c) {
    const Column col = generate_column(seed, c);
    for (int lz = 0; lz < CHUNK; ++lz) {
        for (int lx = 0; lx < CHUNK; ++lx) {
            const int x = c.x * CHUNK + lx, z = c.z * CHUNK + lz;
            const int h = terrain_height(seed, x, z);
            CHECK(h >= 48 && h <= 103);
            if (h < 48 || h > 103) continue;  // the reads below assume a valid height
            const bool sandy = h <= SAND_MAX;
            CHECK(block_at(col, c, x, 0, z) == blk::DEEPSTONE);
            CHECK(block_at(col, c, x, h - 4, z) == blk::STONE);
            CHECK(block_at(col, c, x, h, z) == (sandy ? blk::SAND : blk::GRASS));
            bool stone = true, under_top = true, above = true;
            for (int y = 1; y <= h - 4; ++y) stone = stone && block_at(col, c, x, y, z) == blk::STONE;
            for (int y = h - 3; y <= h - 1; ++y)
                under_top = under_top && block_at(col, c, x, y, z) == (sandy ? blk::SAND : blk::DIRT);
            for (int y = h + 1; y < WORLD_HEIGHT; ++y) {
                const BlockId b = block_at(col, c, x, y, z);
                above = above && (b == blk::AIR || b == blk::LOG || b == blk::LEAVES);
            }
            CHECK(stone);
            CHECK(under_top);
            CHECK(above);
        }
    }
}

// No tree origin other than (ox, oz) within 2 blocks of (x, z), so no other tree can put a log
// or leaves at (x, z).
bool only_origin_near(int x, int z, int ox, int oz) {
    for (int dz = -2; dz <= 2; ++dz)
        for (int dx = -2; dx <= 2; ++dx)
            if ((x + dx != ox || z + dz != oz) && tree_at(SEED, x + dx, z + dz)) return false;
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
    // Low bits pick trees (% 64): over 65,536 cells about 1,024 hit (binomial sd ~32).
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

TEST(test_terrain_height_formula) {
    const std::array<int, 7> xs{-1000000, -97, -1, 0, 1, 95, 1000000};
    for (int x : xs) {
        for (int z = -300; z < 300; z += 7) {
            const float n = fbm2(SEED, static_cast<float>(x) / 96.0f, static_cast<float>(z) / 96.0f, 5);
            const int want = std::clamp(48 + static_cast<int>(n * 56.0f), 48, 103);
            CHECK(terrain_height(SEED, x, z) == want);
            CHECK(terrain_height(SEED, z, x) >= 48 && terrain_height(SEED, z, x) <= 103);
        }
    }
}

TEST(test_tree_at_rule) {
    bool agree = true, saw_hit_at_65 = false, saw_hit_at_66 = false;
    for (int z = -512; z < 512; ++z) {
        for (int x = -512; x < 512; ++x) {
            const bool hit = hash2(SEED ^ 0x7EE5u, x, z) % 64 == 0;
            if (!hit) {
                agree = agree && !tree_at(SEED, x, z);
                continue;
            }
            const int h = terrain_height(SEED, x, z);
            agree = agree && tree_at(SEED, x, z) == (h > SAND_MAX);
            if (h == 65) saw_hit_at_65 = true;  // boundary: sand, no tree
            if (h == 66) saw_hit_at_66 = true;  // boundary: grass, tree
        }
    }
    CHECK(agree);
    CHECK(saw_hit_at_65);
    CHECK(saw_hit_at_66);
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

TEST(test_sand_grass_boundary) {
    bool found65 = false, found66 = false;
    for (int z = 0; z < 1024 && !(found65 && found66); ++z) {
        for (int x = 0; x < 1024 && !(found65 && found66); ++x) {
            const int h = terrain_height(SEED, x, z);
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
                const BlockId above = block_at(col, c, x, 67, z);
                CHECK(above == blk::AIR || above == blk::LOG);
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

TEST(test_trees_exist) {
    int logs = 0;
    for (int cz = 0; cz < 4; ++cz) {
        for (int cx = 0; cx < 4; ++cx) {
            const Column col = generate_column(SEED, ColumnPos{cx, cz});
            for (const auto& ch : col) logs += static_cast<int>(std::count(ch->blocks.begin(), ch->blocks.end(), blk::LOG));
        }
    }
    CHECK(logs >= 1);
}

TEST(test_tree_shape) {
    // An isolated tree (no other origin within 5 blocks) on ground no higher than h+3 within 3
    // blocks, with its 7x7 surroundings inside one column, so every cell checked is exact.
    bool found = false;
    for (int z = 0; z < 1024 && !found; ++z) {
        for (int x = 0; x < 1024 && !found; ++x) {
            const int lx = floor_mod(x, CHUNK), lz = floor_mod(z, CHUNK);
            if (lx < 3 || lx > 12 || lz < 3 || lz > 12 || !tree_at(SEED, x, z)) continue;
            const int h = terrain_height(SEED, x, z);
            bool clear = true;
            for (int dz = -5; dz <= 5 && clear; ++dz) {
                for (int dx = -5; dx <= 5 && clear; ++dx) {
                    if ((dx != 0 || dz != 0) && tree_at(SEED, x + dx, z + dz)) clear = false;
                    if (std::abs(dx) <= 3 && std::abs(dz) <= 3 && terrain_height(SEED, x + dx, z + dz) > h + 3)
                        clear = false;
                }
            }
            if (!clear) continue;
            found = true;

            const ColumnPos c = column_at(x, z);
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
        }
    }
    CHECK(found);
}

TEST(test_tree_canopy_crosses_border) {
    bool found = false;
    int tz = 0;
    for (int z = 0; z < 4096 && !found; ++z) {
        if (tree_at(SEED, 15, z)) {
            found = true;
            tz = z;
        }
    }
    CHECK(found);
    if (!found) return;
    const int h = terrain_height(SEED, 15, tz);
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
    // Origin at z = -16 (first row of column z = -1); leaves reach z = -17 and -18 in column -2.
    bool found = false;
    for (int x = -1; x >= -4096 && !found; --x) {
        if (!tree_at(SEED, x, -16)) continue;
        const int h = terrain_height(SEED, x, -16);
        if (tree_at(SEED, x, -17) || tree_at(SEED, x, -18) || terrain_height(SEED, x, -17) > h + 3 ||
            terrain_height(SEED, x, -18) > h + 3)
            continue;  // that cell could hold a log or ground instead of leaves
        found = true;
        const ColumnPos south = column_at(x, -18);
        CHECK(south.z == -2);
        const Column col = generate_column(SEED, south);
        CHECK(block_at(col, south, x, h + 4, -17) == blk::LEAVES);
        CHECK(block_at(col, south, x, h + 5, -18) == blk::LEAVES);
        CHECK(block_at(col, south, x, h + 6, -17) == blk::LEAVES);
    }
    CHECK(found);
}

TEST(test_tree_canopy_reaches_two_outside) {
    // An origin exactly 2 blocks outside a column still puts leaves into it, on all four sides:
    // x = 14 reaches x = 16 (column 1), x = 17 reaches x = 15 (column 0), and the same along z.
    // No other origin is near the target cell, so only that tree can have put leaves there.
    struct Case {
        int origin, target, column;
        bool along_x;
    };
    const std::array<Case, 4> cases{{{14, 16, 1, true}, {17, 15, 0, true}, {14, 16, 1, false}, {17, 15, 0, false}}};
    for (const Case& k : cases) {
        bool found = false;
        for (int s = 0; s < 4096 && !found; ++s) {
            // Origin (ox, oz) and target (tx, tz) lie 2 apart along the tested axis.
            const int ox = k.along_x ? k.origin : s, oz = k.along_x ? s : k.origin;
            const int tx = k.along_x ? k.target : s, tz = k.along_x ? s : k.target;
            if (!tree_at(SEED, ox, oz)) continue;
            const int h = terrain_height(SEED, ox, oz);
            if (terrain_height(SEED, tx, tz) > h + 3 || !only_origin_near(tx, tz, ox, oz)) continue;
            found = true;
            const ColumnPos c = column_at(tx, tz);
            CHECK((k.along_x ? c.x : c.z) == k.column);
            const Column col = generate_column(SEED, c);
            CHECK(block_at(col, c, tx, h + 4, tz) == blk::LEAVES);
            CHECK(block_at(col, c, tx, h + 5, tz) == blk::LEAVES);
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
