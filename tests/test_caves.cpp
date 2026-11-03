// Tests for the cave and ravine part of src/terrain.* (M2 Task 3).
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "terrain.hpp"
#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

constexpr uint32_t SEED = 7;

BlockId block_at(const Column& col, ColumnPos c, int x, int y, int z) {
    return col[static_cast<std::size_t>(y / CHUNK)]->get(x - c.x * CHUNK, y % CHUNK, z - c.z * CHUNK);
}

bool same_column(const Column& a, const Column& b) {
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i]->blocks != b[i]->blocks) return false;
    return true;
}

constexpr GenOptions NO_CARVING{false, false};
constexpr GenOptions RAVINES_ONLY{false, true};

// The spaghetti-cave noise condition, written from the spec formula.
bool cave_noise(uint32_t seed, int x, int y, int z) {
    const float fx = static_cast<float>(x) / 48.f, fy = static_cast<float>(y) / 32.f,
                fz = static_cast<float>(z) / 48.f;
    return std::abs(fbm3(seed ^ 0xCA7Eu, fx, fy, fz, 3) - 0.5f) < 0.035f &&
           std::abs(fbm3(seed ^ 0x5A17u, fx, fy, fz, 3) - 0.5f) < 0.035f;
}

// Longest run of AIR in y = 1..top at (x, z).
int longest_air_run(const Column& col, ColumnPos c, int x, int z, int top) {
    int best = 0, run = 0;
    for (int y = 1; y <= top; ++y) {
        run = block_at(col, c, x, y, z) == blk::AIR ? run + 1 : 0;
        best = std::max(best, run);
    }
    return best;
}

}  // namespace

TEST(test_cave_fraction) {
    long stone = 0, carved = 0;
    for (int cz = 0; cz < 8; ++cz)
        for (int cx = 0; cx < 8; ++cx) {
            const ColumnPos c{cx, cz};
            const Column plain = generate_column(SEED, c, NO_CARVING);
            const Column cut = generate_column(SEED, c);
            for (std::size_t s = 0; s < plain.size(); ++s)
                for (std::size_t i = 0; i < plain[s]->blocks.size(); ++i) {
                    if (plain[s]->blocks[i] != blk::STONE) continue;
                    ++stone;
                    carved += cut[s]->blocks[i] != blk::STONE ? 1 : 0;
                }
        }
    CHECK(stone > 0);
    CHECK(carved * 100 >= stone * 2);
    CHECK(carved * 100 <= stone * 12);
}

TEST(test_cave_no_water_breach) {
    bool breach = false;
    for (int cz = 0; cz < 8; ++cz)
        for (int cx = 0; cx < 8; ++cx) {
            const ColumnPos c{cx, cz};
            const Column col = generate_column(SEED, c);
            for (int z = cz * CHUNK; z < cz * CHUNK + CHUNK; ++z)
                for (int x = cx * CHUNK; x < cx * CHUNK + CHUNK; ++x)
                    for (int y = 1; y < WORLD_HEIGHT - 1; ++y)
                        breach = breach || (block_at(col, c, x, y, z) == blk::AIR &&
                                            block_at(col, c, x, y + 1, z) == blk::WATER);
        }
    CHECK(!breach);
}

TEST(test_cave_order_independent) {
    const Column east_alone = generate_column(SEED, ColumnPos{1, 0});
    const Column west = generate_column(SEED, ColumnPos{0, 0});
    const Column east_after = generate_column(SEED, ColumnPos{1, 0});
    CHECK(same_column(east_alone, east_after));
    CHECK(same_column(west, generate_column(SEED, ColumnPos{0, 0})));

    // A ravine reaching two columns: each column is the same whichever is generated first.
    // a holds the ravine's origin; b is a neighbor the ravine (at least 48 long) also carves.
    bool found = false;
    for (int cz = 0; cz < 64 && !found; cz += 4)
        for (int cx = 0; cx < 64 && !found; cx += 4) {
            const std::vector<Ravine> r = ravines_near(SEED, ColumnPos{cx, cz});
            if (r.empty()) continue;
            found = true;
            const ColumnPos a{floor_div(r[0].x0, CHUNK), floor_div(r[0].z0, CHUNK)};
            ColumnPos b = a;
            bool crosses = false;
            for (int dz = -1; dz <= 1 && !crosses; ++dz)
                for (int dx = -1; dx <= 1 && !crosses; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    b = ColumnPos{a.x + dx, a.z + dz};
                    crosses = !same_column(generate_column(SEED, b, RAVINES_ONLY), generate_column(SEED, b, NO_CARVING));
                }
            CHECK(crosses);
            CHECK(!same_column(generate_column(SEED, a, RAVINES_ONLY), generate_column(SEED, a, NO_CARVING)));
            const Column a1 = generate_column(SEED, a);
            const Column b1 = generate_column(SEED, b);
            const Column b2 = generate_column(SEED, b);
            const Column a2 = generate_column(SEED, a);
            CHECK(same_column(a1, a2));
            CHECK(same_column(b1, b2));
        }
    CHECK(found);
}

TEST(test_ravine_found) {
    // 16 x 16 = 256 ravine cells of 64 x 64 blocks. The first ravine found opens an AIR run of
    // at least 15 below the surface in its own column, at its origin (always carved; caves off,
    // so only the ravine can make the run).
    bool found = false, deep = false;
    for (int gz = 0; gz < 16 && !found; ++gz)
        for (int gx = 0; gx < 16 && !found; ++gx) {
            for (const Ravine& r : ravines_near(SEED, ColumnPos{gx * 4, gz * 4})) {
                if (floor_div(r.x0, 64) != gx || floor_div(r.z0, 64) != gz) continue;  // its own cell
                found = true;
                const ColumnPos o{floor_div(r.x0, CHUNK), floor_div(r.z0, CHUNK)};
                const Column col = generate_column(SEED, o, RAVINES_ONLY);
                deep = longest_air_run(col, o, r.x0, r.z0, terrain_height(SEED, r.x0, r.z0)) >= 15;
                break;
            }
        }
    CHECK(found);
    CHECK(deep);
}

TEST(test_cave_keeps_deepstone) {
    bool deepstone = true;
    for (int cz = -4; cz < 4; ++cz)
        for (int cx = -4; cx < 4; ++cx) {
            const ColumnPos c{cx, cz};
            const Column col = generate_column(SEED, c);
            for (int z = cz * CHUNK; z < cz * CHUNK + CHUNK; ++z)
                for (int x = cx * CHUNK; x < cx * CHUNK + CHUNK; ++x)
                    deepstone = deepstone && block_at(col, c, x, 0, z) == blk::DEEPSTONE;
        }
    CHECK(deepstone);
}

TEST(test_cave_noise3_range) {
    bool in_range = true, repeats = true;
    for (int i = 0; i < 10000; ++i) {
        const float x = static_cast<float>(i % 97) * 0.37f - 18.f, y = static_cast<float>(i % 31) * 0.53f,
                    z = static_cast<float>(i / 97) * 0.41f - 20.f;
        const float v = fbm3(SEED, x, y, z, 3);
        in_range = in_range && v >= 0.f && v <= 1.f;
        repeats = repeats && fbm3(SEED, x, y, z, 3) == v;
        const float n = value_noise3(SEED, x, y, z);
        in_range = in_range && n >= 0.f && n <= 1.f;
    }
    CHECK(in_range);
    CHECK(repeats);
}

TEST(test_ravine_width_formula) {
    CHECK(ravine_width(0.f) == 2.f);
    CHECK(ravine_width(0.5f) == 4.f);
    CHECK(ravine_width(1.f) == 2.f);
    CHECK(ravine_width(0.25f) == 3.5f);
}

// ---- Coverage Checklist additions ----

TEST(test_cave_formula_and_depth_limits) {
    // is_cave is the noise condition, only for 5 <= y <= surface_h - 4.
    int hits = 0;
    bool agree = true;
    for (int z = -400; z < 400; z += 3)
        for (int x = -400; x < 400; x += 5)
            for (int y : {4, 5, 6, 40, 96, 97}) {
                const bool noise = cave_noise(SEED, x, y, z);
                hits += noise ? 1 : 0;
                agree = agree && is_cave(SEED, x, y, z, 200) == (noise && y >= 5);
                agree = agree && is_cave(SEED, x, y, z, 100) == (noise && y >= 5 && y <= 96);
            }
    CHECK(hits > 0);
    CHECK(agree);
}

TEST(test_cave_noise3_continuity_and_seeds) {
    bool smooth = true, seeds_differ = false;
    for (int i = 0; i < 1000; ++i) {
        const float x = static_cast<float>(i) * 0.173f - 50.f, y = static_cast<float>(i % 17) * 0.7f,
                    z = static_cast<float>(i % 23) * 1.3f - 9.f;
        smooth = smooth && std::abs(value_noise3(3, x, y, z) - value_noise3(3, x, y + 0.01f, z)) < 0.05f;
        seeds_differ = seeds_differ || value_noise3(1, x, y, z) != value_noise3(2, x, y, z);
    }
    CHECK(smooth);
    CHECK(seeds_differ);
}

TEST(test_ravines_near_ranges) {
    // Parameters stay in their documented ranges, origins lie in the 5x5 ravine cells around the
    // column's cell, and every column of one 64-block cell sees the same list.
    int total = 0;
    bool ok = true;
    for (int cz = -32; cz < 32; cz += 4)
        for (int cx = -32; cx < 32; cx += 4) {
            const ColumnPos c{cx, cz};
            const std::vector<Ravine> r = ravines_near(SEED, c);
            ok = ok && r.size() <= 25;
            const std::vector<Ravine> same_cell = ravines_near(SEED, ColumnPos{cx + 3, cz + 3});
            ok = ok && same_cell.size() == r.size();
            for (std::size_t i = 0; i < r.size(); ++i) {
                ++total;
                const Ravine& v = r[i];
                ok = ok && std::abs(floor_div(v.x0, 64) - floor_div(cx, 4)) <= 2;
                ok = ok && std::abs(floor_div(v.z0, 64) - floor_div(cz, 4)) <= 2;
                ok = ok && v.angle >= 0.f && v.angle < 1.f;
                ok = ok && v.length >= 48 && v.length <= 111;
                ok = ok && v.depth >= 20 && v.depth <= 30;
                ok = ok && v.width == 4;
                if (i < same_cell.size())
                    ok = ok && same_cell[i].x0 == v.x0 && same_cell[i].z0 == v.z0 &&
                         same_cell[i].angle == v.angle && same_cell[i].length == v.length;
            }
        }
    CHECK(total > 0);
    CHECK(ok);
}

TEST(test_cave_negative_border_order) {
    // Column (-1, 0) is identical whether (0, 0) is generated before or after it.
    const Column first = generate_column(SEED, ColumnPos{-1, 0});
    (void)generate_column(SEED, ColumnPos{0, 0});
    CHECK(same_column(first, generate_column(SEED, ColumnPos{-1, 0})));
}

TEST(test_cave_carving_only_below_surface) {
    // Carving changes only cells at or below the surface height and never touches y = 0; every
    // carved cell becomes AIR.
    bool ok = true;
    for (int cx = -2; cx < 2; ++cx) {
        const ColumnPos c{cx, 3};
        const Column plain = generate_column(SEED, c, NO_CARVING);
        const Column cut = generate_column(SEED, c);
        for (int z = c.z * CHUNK; z < c.z * CHUNK + CHUNK; ++z)
            for (int x = c.x * CHUNK; x < c.x * CHUNK + CHUNK; ++x) {
                const int h = terrain_height(SEED, x, z);
                for (int y = 0; y < WORLD_HEIGHT; ++y) {
                    const BlockId a = block_at(plain, c, x, y, z), b = block_at(cut, c, x, y, z);
                    if (a == b) continue;
                    ok = ok && y >= 1 && y <= h && b == blk::AIR;
                }
            }
    }
    CHECK(ok);
}

TEST(test_ravine_under_water_keeps_roof) {
    // A ravine whose origin (always carved: t = 0, distance 0) lies under water. The 3 cells
    // below the water stay solid, the 4th is carved, and no AIR anywhere has WATER above it.
    bool found = false;
    for (int gz = -32; gz < 32 && !found; ++gz)
        for (int gx = -32; gx < 32 && !found; ++gx)
            for (const Ravine& r : ravines_near(SEED, ColumnPos{gx * 4, gz * 4})) {
                if (floor_div(r.x0, 64) != gx || floor_div(r.z0, 64) != gz) continue;
                const int h = terrain_height(SEED, r.x0, r.z0);
                if (h >= SEA_LEVEL - 1) continue;
                found = true;
                const ColumnPos c{floor_div(r.x0, CHUNK), floor_div(r.z0, CHUNK)};
                const Column col = generate_column(SEED, c, RAVINES_ONLY);
                CHECK(block_at(col, c, r.x0, h + 1, r.z0) == blk::WATER);
                for (int y = h - 2; y <= h; ++y) CHECK(block_at(col, c, r.x0, y, r.z0) != blk::AIR);
                CHECK(block_at(col, c, r.x0, h - 3, r.z0) == blk::AIR);
                bool breach = false;
                for (int z = c.z * CHUNK; z < c.z * CHUNK + CHUNK; ++z)
                    for (int x = c.x * CHUNK; x < c.x * CHUNK + CHUNK; ++x)
                        for (int y = 1; y < WORLD_HEIGHT - 1; ++y)
                            breach = breach || (block_at(col, c, x, y, z) == blk::AIR &&
                                                block_at(col, c, x, y + 1, z) == blk::WATER);
                CHECK(!breach);
                break;
            }
    CHECK(found);
}

namespace {

// The documented ravine direction: fraction `angle` around (1,-1) -> (1,1) -> (-1,1) -> (-1,-1).
void spec_direction(float angle, float& dx, float& dz) {
    const float a = angle * 4.f, side = std::floor(a), f = a - side;
    float px = -1.f + 2.f * f, pz = -1.f;
    if (side < 1.f) {
        px = 1.f;
        pz = -1.f + 2.f * f;
    } else if (side < 2.f) {
        px = 1.f - 2.f * f;
        pz = 1.f;
    } else if (side < 3.f) {
        px = -1.f;
        pz = 1.f - 2.f * f;
    }
    const float len = std::sqrt(px * px + pz * pz);
    dx = px / len;
    dz = pz / len;
}

}  // namespace

TEST(test_ravine_cross_section) {
    // An isolated ravine (the only one any nearby column sees) on dry land: a surface cell is
    // carved exactly when it lies along the ravine (0 < t < 1) within ravine_width(t) / 2 of
    // its line. Cells within 0.01 of an edge are skipped.
    bool found = false;
    int inside = 0, outside = 0;
    for (int gz = -32; gz < 32 && !found; ++gz)
        for (int gx = -32; gx < 32 && !found; ++gx) {
            const std::vector<Ravine> nearby = ravines_near(SEED, ColumnPos{gx * 4, gz * 4});
            if (nearby.size() != 1) continue;
            const Ravine r = nearby[0];
            if (floor_div(r.x0, 64) != gx || floor_div(r.z0, 64) != gz) continue;
            if (terrain_height(SEED, r.x0, r.z0) <= SEA_LEVEL + 4) continue;
            const ColumnPos o{floor_div(r.x0, CHUNK), floor_div(r.z0, CHUNK)};
            bool isolated = true;
            for (int dz = -8; dz <= 8 && isolated; ++dz)
                for (int dx = -8; dx <= 8 && isolated; ++dx)
                    isolated = ravines_near(SEED, ColumnPos{o.x + dx, o.z + dz}).size() <= 1;
            if (!isolated) continue;
            found = true;
            float dx = 0.f, dz = 0.f;
            spec_direction(r.angle, dx, dz);
            for (int cz = -2; cz <= 2; ++cz)
                for (int cx = -2; cx <= 2; ++cx) {
                    const ColumnPos c{o.x + cx, o.z + cz};
                    const Column col = generate_column(SEED, c, RAVINES_ONLY);
                    for (int z = c.z * CHUNK; z < c.z * CHUNK + CHUNK; ++z)
                        for (int x = c.x * CHUNK; x < c.x * CHUNK + CHUNK; ++x) {
                            const int h = terrain_height(SEED, x, z);
                            if (h <= SEA_LEVEL + 4) continue;  // no water within reach
                            const float rx = static_cast<float>(x - r.x0), rz = static_cast<float>(z - r.z0);
                            const float t = (rx * dx + rz * dz) / static_cast<float>(r.length);
                            const float half = ravine_width(t) / 2.f;
                            const float across = std::abs(rz * dx - rx * dz);
                            if (t < 0.01f || t > 0.99f || std::abs(across - half) < 0.01f) continue;
                            const bool want = across < half;
                            CHECK((block_at(col, c, x, h, z) == blk::AIR) == want);
                            (want ? inside : outside) += 1;
                        }
                }
        }
    CHECK(found);
    CHECK(inside > 0);
    CHECK(outside > 0);
}

TEST(test_cave_extreme_columns) {
    // Default generation (caves and ravines on) at the corners of the supported range
    // |pos| <= 2^27 - 2 runs without overflow (UBSan) and keeps the carving rules.
    constexpr int M = (1 << 27) - 2;
    const ColumnPos corners[4] = {{M, M}, {-M, -M}, {M, -M}, {-M, M}};
    for (ColumnPos c : corners) {
        const Column col = generate_column(SEED, c);
        bool deepstone = true, breach = false;
        for (int z = c.z * CHUNK; z < c.z * CHUNK + CHUNK; ++z)
            for (int x = c.x * CHUNK; x < c.x * CHUNK + CHUNK; ++x) {
                deepstone = deepstone && block_at(col, c, x, 0, z) == blk::DEEPSTONE;
                for (int y = 1; y < WORLD_HEIGHT - 1; ++y)
                    breach = breach || (block_at(col, c, x, y, z) == blk::AIR &&
                                        block_at(col, c, x, y + 1, z) == blk::WATER);
            }
        CHECK(deepstone);
        CHECK(!breach);
        for (const Ravine& r : ravines_near(SEED, c)) {
            CHECK(std::abs(floor_div(r.x0, 64) - floor_div(c.x, 4)) <= 2);
            CHECK(std::abs(floor_div(r.z0, 64) - floor_div(c.z, 4)) <= 2);
        }
    }
}
