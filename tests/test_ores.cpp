// Tests for the ore and quartz-geode part of src/terrain.* (M2 Task 4).
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "terrain.hpp"
#include "test.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t SEED = 7;

constexpr GenOptions LAYERS_ONLY{false, false, false, false, false};
constexpr GenOptions GEODES_ONLY{false, false, false, true, false};

BlockId block_at(const Column& col, ColumnPos c, int x, int y, int z) {
    return col[static_cast<std::size_t>(y / CHUNK)]->get(x - c.x * CHUNK, y % CHUNK, z - c.z * CHUNK);
}

bool same_column(const Column& a, const Column& b) {
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i]->blocks != b[i]->blocks) return false;
    return true;
}

bool is_ore(BlockId b) {
    return b == blk::COAL_ORE || b == blk::COPPER_ORE || b == blk::TIN_ORE || b == blk::IRON_ORE ||
           b == blk::SILVER_ORE;
}

// Spec depth table (design.md, Ores), indexed by ore id - COAL_ORE.
struct Depth {
    BlockId ore;
    int lo, hi;
};
constexpr Depth SPEC_DEPTH[5] = {{blk::COAL_ORE, 5, 140},
                                 {blk::COPPER_ORE, 20, 100},
                                 {blk::TIN_ORE, 10, 70},
                                 {blk::IRON_ORE, 5, 60},
                                 {blk::SILVER_ORE, 5, 30}};

const Depth& depth_of(BlockId ore) {
    return SPEC_DEPTH[ore - blk::COAL_ORE];
}

// A column of STONE from y = 1 to 255 over DEEPSTONE, with pos set.
Column stone_column(ColumnPos c) {
    Column col;
    for (int cy = 0; cy < SECTIONS; ++cy) {
        col[static_cast<std::size_t>(cy)] = std::make_unique<Chunk>();
        col[static_cast<std::size_t>(cy)]->pos = ChunkPos{c.x, cy, c.z};
        col[static_cast<std::size_t>(cy)]->blocks.fill(blk::STONE);
    }
    for (int lz = 0; lz < CHUNK; ++lz)
        for (int lx = 0; lx < CHUNK; ++lx) col[0]->set(lx, 0, lz, blk::DEEPSTONE);
    return col;
}

// Generates columns on demand with fixed options and reads world blocks from them.
struct Sampler {
    GenOptions opt;
    std::map<std::pair<int, int>, Column> cols;

    BlockId at(int x, int y, int z) {
        const ColumnPos c{floor_div(x, CHUNK), floor_div(z, CHUNK)};
        auto it = cols.find({c.x, c.z});
        if (it == cols.end()) it = cols.emplace(std::make_pair(c.x, c.z), generate_column(SEED, c, opt)).first;
        return block_at(it->second, c, x, y, z);
    }
};

// Ore counts over columns (0..7, 0..7), indexed by ore id - COAL_ORE.
std::vector<long> ore_counts() {
    std::vector<long> n(5, 0);
    for (int cz = 0; cz < 8; ++cz)
        for (int cx = 0; cx < 8; ++cx) {
            const Column col = generate_column(SEED, ColumnPos{cx, cz});
            for (const auto& ch : col)
                for (BlockId b : ch->blocks)
                    if (is_ore(b)) ++n[static_cast<std::size_t>(b - blk::COAL_ORE)];
        }
    return n;
}

// True when another geode centre lies within `d` blocks (Chebyshev) of g.
bool crowded(const std::vector<Geode>& all, const Geode& g, int d) {
    for (const Geode& o : all) {
        if (o.x == g.x && o.y == g.y && o.z == g.z) continue;
        if (std::abs(o.x - g.x) <= d && std::abs(o.y - g.y) <= d && std::abs(o.z - g.z) <= d) return true;
    }
    return false;
}

}  // namespace

TEST(test_ore_rules_table) {
    CHECK(ORE_RULES.size() == 5);
    for (std::size_t i = 0; i < ORE_RULES.size(); ++i) {
        const OreRule& r = ORE_RULES[i];
        CHECK(r.ore == SPEC_DEPTH[i].ore);
        CHECK(r.y_min == SPEC_DEPTH[i].lo);
        CHECK(r.y_max == SPEC_DEPTH[i].hi);
        CHECK(r.y_min >= 1);
    }
    CHECK(ORE_RULES[0].veins_per_column == 20 && ORE_RULES[0].vein_size == 8);
    CHECK(ORE_RULES[1].veins_per_column == 10 && ORE_RULES[1].vein_size == 6);
    CHECK(ORE_RULES[2].veins_per_column == 7 && ORE_RULES[2].vein_size == 5);
    CHECK(ORE_RULES[3].veins_per_column == 8 && ORE_RULES[3].vein_size == 6);
    CHECK(ORE_RULES[4].veins_per_column == 2 && ORE_RULES[4].vein_size == 4);
}

TEST(test_ore_depth_ranges) {
    bool ok = true;
    long found = 0;
    for (int cz = 0; cz < 8; ++cz)
        for (int cx = 0; cx < 8; ++cx) {
            const Column col = generate_column(SEED, ColumnPos{cx, cz});
            for (int y = 0; y < WORLD_HEIGHT; ++y)
                for (int lz = 0; lz < CHUNK; ++lz)
                    for (int lx = 0; lx < CHUNK; ++lx) {
                        const BlockId b = col[static_cast<std::size_t>(y / CHUNK)]->get(lx, y % CHUNK, lz);
                        if (!is_ore(b)) continue;
                        ++found;
                        ok = ok && y >= depth_of(b).lo - 2 && y <= depth_of(b).hi + 2;
                    }
        }
    CHECK(found > 0);
    CHECK(ok);
}

TEST(test_ore_relative_abundance) {
    const std::vector<long> n = ore_counts();
    const long coal = n[0], copper = n[1], tin = n[2], iron = n[3], silver = n[4];
    CHECK(coal > copper);
    CHECK(copper > iron);
    CHECK(iron > silver);
    CHECK(silver > 0);
    CHECK(tin > 0);
}

TEST(test_ore_replaces_stone_only) {
    long changed = 0;
    bool ok = true;
    for (int cz = -2; cz < 2; ++cz)
        for (int cx = -2; cx < 2; ++cx) {
            const ColumnPos c{cx, cz};
            // Features off on both sides: a glowcap needs STONE under it, so ores change them (M3).
            const Column plain = generate_column(SEED, c, GenOptions{true, true, false, true, false});
            const Column with = generate_column(SEED, c, GenOptions{true, true, true, true, false});
            for (std::size_t s = 0; s < plain.size(); ++s)
                for (std::size_t i = 0; i < plain[s]->blocks.size(); ++i) {
                    if (plain[s]->blocks[i] == with[s]->blocks[i]) continue;
                    ++changed;
                    ok = ok && plain[s]->blocks[i] == blk::STONE && is_ore(with[s]->blocks[i]);
                }
        }
    CHECK(changed > 0);
    CHECK(ok);
}

TEST(test_ore_vein_crosses_border) {
    // On all-STONE columns, a 2x2x2 blob of one ore straddling the border between a column and
    // its +x neighbor shows that veins reach into neighbor columns.
    int straddles = 0;
    for (int cx = -4; cx < 4; ++cx) {
        const ColumnPos a{cx, 0}, b{cx + 1, 0};
        Column ca = stone_column(a), cb = stone_column(b);
        place_ores(ca, a, SEED);
        place_ores(cb, b, SEED);
        for (int y = 1; y < WORLD_HEIGHT - 1; ++y)
            for (int lz = 0; lz < CHUNK - 1; ++lz) {
                const BlockId o = block_at(ca, a, a.x * CHUNK + 15, y, lz);
                if (!is_ore(o)) continue;
                bool cube = true;
                for (int dy = 0; dy <= 1; ++dy)
                    for (int dz = 0; dz <= 1; ++dz)
                        cube = cube && block_at(ca, a, a.x * CHUNK + 15, y + dy, lz + dz) == o &&
                               block_at(cb, b, b.x * CHUNK, y + dy, lz + dz) == o;
                straddles += cube ? 1 : 0;
            }
    }
    CHECK(straddles > 0);

    // Order independence across the border at x = 0 and at x = -16 (negative columns).
    for (int ax : {0, -1}) {
        const ColumnPos a{ax, 0}, b{ax + 1, 0};
        const Column b_alone = generate_column(SEED, b);
        const Column a1 = generate_column(SEED, a);
        const Column b_after = generate_column(SEED, b);
        CHECK(same_column(b_alone, b_after));
        CHECK(same_column(a1, generate_column(SEED, a)));
    }
}

TEST(test_geode_found) {
    // A geode deep in stone: QUARTZ 4 blocks from the centre along each axis, AIR at the centre.
    bool found = false;
    for (int gz = -8; gz < 8 && !found; ++gz)
        for (int gx = -8; gx < 8 && !found; ++gx) {
            const std::vector<Geode> all = geodes_near(SEED, ColumnPos{gx * 4, gz * 4});
            for (const Geode& g : all) {
                if (crowded(all, g, 10)) continue;
                bool deep = true;
                for (int dz = -4; dz <= 4; ++dz)
                    for (int dx = -4; dx <= 4; ++dx)
                        deep = deep && terrain_height(SEED, g.x + dx, g.z + dz) - 4 > g.y + 4;
                if (!deep) continue;
                found = true;
                Sampler s{GEODES_ONLY, {}};
                CHECK(s.at(g.x, g.y, g.z) == blk::AIR);
                CHECK(s.at(g.x + 4, g.y, g.z) == blk::QUARTZ);
                CHECK(s.at(g.x - 4, g.y, g.z) == blk::QUARTZ);
                CHECK(s.at(g.x, g.y + 4, g.z) == blk::QUARTZ);
                CHECK(s.at(g.x, g.y - 4, g.z) == blk::QUARTZ);
                CHECK(s.at(g.x, g.y, g.z + 4) == blk::QUARTZ);
                CHECK(s.at(g.x, g.y, g.z - 4) == blk::QUARTZ);
                CHECK(s.at(g.x + 5, g.y, g.z) == blk::STONE);
                CHECK(s.at(g.x, g.y - 5, g.z) == blk::STONE);
                break;
            }
        }
    CHECK(found);
}

TEST(test_geode_inside_world) {
    // 64 x 64 columns = 16 x 16 geode cells. Each geode is listed once, by its own cell; the
    // cells it writes (geodes only versus layers only, over its 11x11x11 box) all lie in y 1-45,
    // and y = 0 stays DEEPSTONE.
    int total = 0;
    bool ok = true;
    Sampler plain{LAYERS_ONLY, {}}, geo{GEODES_ONLY, {}};
    for (int cz = -32; cz < 32; cz += 4)
        for (int cx = -32; cx < 32; cx += 4)
            for (const Geode& g : geodes_near(SEED, ColumnPos{cx, cz})) {
                if (floor_div(g.x, 64) != floor_div(cx, 4) || floor_div(g.z, 64) != floor_div(cz, 4)) continue;
                ++total;
                ok = ok && g.y >= 5 && g.y <= 40;
                for (int y = std::max(0, g.y - 5); y <= g.y + 5; ++y)
                    for (int dz = -5; dz <= 5; ++dz)
                        for (int dx = -5; dx <= 5; ++dx) {
                            const int x = g.x + dx, z = g.z + dz;
                            if (geo.at(x, y, z) != plain.at(x, y, z)) ok = ok && y >= 1 && y <= 45;
                            if (y == 0) ok = ok && geo.at(x, 0, z) == blk::DEEPSTONE;
                        }
            }
    CHECK(total > 0);
    CHECK(ok);
}

// ---- Coverage Checklist additions ----

TEST(test_ore_spec_depth_exact) {
    // Boundaries: every ore block lies inside its spec depth range exactly, and the walk reaches
    // the bottom of coal's range (y = 5) somewhere in 16 all-STONE columns.
    bool ok = true;
    bool coal_low = false;
    for (int cz = 0; cz < 4; ++cz)
        for (int cx = 0; cx < 4; ++cx) {
            const ColumnPos c{cx - 2, cz - 2};
            Column col = stone_column(c);
            place_ores(col, c, SEED);
            for (int y = 0; y < WORLD_HEIGHT; ++y)
                for (int lz = 0; lz < CHUNK; ++lz)
                    for (int lx = 0; lx < CHUNK; ++lx) {
                        const BlockId b = col[static_cast<std::size_t>(y / CHUNK)]->get(lx, y % CHUNK, lz);
                        if (!is_ore(b)) continue;
                        ok = ok && y >= depth_of(b).lo && y <= depth_of(b).hi;
                        coal_low = coal_low || (b == blk::COAL_ORE && y == 5);
                    }
        }
    CHECK(ok);
    CHECK(coal_low);
}

TEST(test_ore_walk_top_boundary) {
    // Over 64 all-STONE columns no ore ever lies above its rule's y_max (the walk's upward clamp).
    bool ok = true;
    long found = 0;
    for (int cz = 0; cz < 8; ++cz)
        for (int cx = 0; cx < 8; ++cx) {
            const ColumnPos c{cx, cz};
            Column col = stone_column(c);
            place_ores(col, c, SEED);
            for (int y = 0; y < WORLD_HEIGHT; ++y)
                for (int lz = 0; lz < CHUNK; ++lz)
                    for (int lx = 0; lx < CHUNK; ++lx) {
                        const BlockId b = col[static_cast<std::size_t>(y / CHUNK)]->get(lx, y % CHUNK, lz);
                        if (!is_ore(b)) continue;
                        ++found;
                        ok = ok && y <= depth_of(b).hi;
                    }
        }
    CHECK(found > 0);
    CHECK(ok);
}

TEST(test_ore_stage_matches_generation) {
    // place_ores and place_geodes are exactly the generation stages: applying them to a column
    // generated without them gives the default column. The features come last (M3 glowcaps read
    // the floor the ores leave), so they are applied after the ores here too.
    for (ColumnPos c : {ColumnPos{0, 0}, ColumnPos{-3, 2}}) {
        Column col = generate_column(SEED, c, GenOptions{true, true, false, false, false});
        place_geodes(col, c, SEED);
        place_ores(col, c, SEED);
        for (const Feature& f : features_near(SEED, c, 3)) place_feature(col, c, f);
        CHECK(same_column(col, generate_column(SEED, c)));
    }
}

TEST(test_ore_only_in_stone_cells) {
    // On a column that holds no STONE, place_ores changes nothing.
    const ColumnPos c{2, 2};
    Column col = stone_column(c);
    for (auto& ch : col) ch->blocks.fill(blk::DIRT);
    place_ores(col, c, SEED);
    bool unchanged = true;
    for (auto& ch : col)
        for (BlockId b : ch->blocks) unchanged = unchanged && b == blk::DIRT;
    CHECK(unchanged);
    place_geodes(col, c, SEED);
    for (auto& ch : col)
        for (BlockId b : ch->blocks) unchanged = unchanged && b == blk::DIRT;
    CHECK(unchanged);
}

TEST(test_geode_shape) {
    // Against layers only, a lone geode changes exactly the STONE cells within distance 4 of its
    // centre: QUARTZ where 9 < d^2 <= 16, AIR where d^2 <= 9.
    bool found = false;
    for (int gz = -8; gz < 8 && !found; ++gz)
        for (int gx = -8; gx < 8 && !found; ++gx) {
            const std::vector<Geode> all = geodes_near(SEED, ColumnPos{gx * 4, gz * 4});
            for (const Geode& g : all) {
                if (crowded(all, g, 12)) continue;
                found = true;
                Sampler plain{LAYERS_ONLY, {}}, geo{GEODES_ONLY, {}};
                int shell = 0, inner = 0;
                bool ok = true;
                for (int dy = -6; dy <= 6; ++dy)
                    for (int dz = -6; dz <= 6; ++dz)
                        for (int dx = -6; dx <= 6; ++dx) {
                            const int x = g.x + dx, y = g.y + dy, z = g.z + dz;
                            const BlockId a = plain.at(x, y, z), b = geo.at(x, y, z);
                            const int d2 = dx * dx + dy * dy + dz * dz;
                            BlockId want = a;
                            if (a == blk::STONE && d2 <= 16) want = d2 > 9 ? blk::QUARTZ : blk::AIR;
                            ok = ok && b == want;
                            shell += (want == blk::QUARTZ) ? 1 : 0;
                            inner += (a == blk::STONE && d2 <= 9) ? 1 : 0;
                        }
                CHECK(ok);
                CHECK(shell > 0);
                CHECK(inner > 0);
                break;
            }
        }
    CHECK(found);
}

TEST(test_geodes_near_ranges) {
    // Centres lie in the 3x3 geode cells (64 x 64 blocks) around the column's cell, every column of
    // one cell sees the same list, and two calls agree.
    int total = 0;
    bool ok = true;
    for (int cz = -40; cz < 40; cz += 4)
        for (int cx = -40; cx < 40; cx += 4) {
            const std::vector<Geode> g = geodes_near(SEED, ColumnPos{cx, cz});
            const std::vector<Geode> same_cell = geodes_near(SEED, ColumnPos{cx + 3, cz + 3});
            ok = ok && g.size() <= 9 && same_cell.size() == g.size();
            for (std::size_t i = 0; i < g.size(); ++i) {
                ++total;
                ok = ok && std::abs(floor_div(g[i].x, 64) - floor_div(cx, 4)) <= 1;
                ok = ok && std::abs(floor_div(g[i].z, 64) - floor_div(cz, 4)) <= 1;
                ok = ok && g[i].y >= 5 && g[i].y <= 40;
                if (i < same_cell.size())
                    ok = ok && same_cell[i].x == g[i].x && same_cell[i].y == g[i].y && same_cell[i].z == g[i].z;
            }
        }
    CHECK(total > 0);
    CHECK(ok);
}

TEST(test_ore_extreme_columns) {
    // Ores and geodes at the corners of the supported range |pos| <= 2^27 - 2 run without
    // overflow (UBSan), keep spec depths, and keep y = 0 DEEPSTONE.
    constexpr int M = (1 << 27) - 2;
    const ColumnPos corners[4] = {{M, M}, {-M, -M}, {M, -M}, {-M, M}};
    for (ColumnPos c : corners) {
        Column col = stone_column(c);
        place_geodes(col, c, SEED);
        place_ores(col, c, SEED);
        bool ok = true;
        long ores = 0;
        for (int y = 0; y < WORLD_HEIGHT; ++y)
            for (int lz = 0; lz < CHUNK; ++lz)
                for (int lx = 0; lx < CHUNK; ++lx) {
                    const BlockId b = col[static_cast<std::size_t>(y / CHUNK)]->get(lx, y % CHUNK, lz);
                    if (y == 0) ok = ok && b == blk::DEEPSTONE;
                    if (!is_ore(b)) continue;
                    ++ores;
                    ok = ok && y >= depth_of(b).lo && y <= depth_of(b).hi;
                }
        CHECK(ok);
        CHECK(ores > 0);
        for (const Geode& g : geodes_near(SEED, c)) CHECK(g.y >= 5 && g.y <= 40);
    }
}
