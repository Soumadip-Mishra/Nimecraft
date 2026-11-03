// Tests for the biome-feature part of src/terrain.* (M2 Task 5): trees, cacti, and plants.
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
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t SEED = 7;
constexpr GenOptions NO_FEATURES{true, true, true, true, false};

BlockId block_at(const Column& col, ColumnPos c, int x, int y, int z) {
    return col[static_cast<std::size_t>(y / CHUNK)]->get(x - c.x * CHUNK, y % CHUNK, z - c.z * CHUNK);
}

bool same_column(const Column& a, const Column& b) {
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i]->blocks != b[i]->blocks) return false;
    return true;
}

ColumnPos column_at(int x, int z) {
    return ColumnPos{floor_div(x, CHUNK), floor_div(z, CHUNK)};
}

bool is_tree(FeatureType t) {
    return t == FeatureType::OakTree || t == FeatureType::PineTree;
}

// Up to n columns whose centre and four corners all lie in biome b, scanning every second column
// of [-256, 256)^2, z-major.
std::vector<ColumnPos> biome_columns(Biome b, std::size_t n) {
    std::vector<ColumnPos> out;
    for (int cz = -256; cz < 256 && out.size() < n; cz += 2)
        for (int cx = -256; cx < 256 && out.size() < n; cx += 2) {
            const int x = cx * CHUNK, z = cz * CHUNK;
            if (biome_at(SEED, x + 8, z + 8) != b || biome_at(SEED, x, z) != b || biome_at(SEED, x + 15, z) != b ||
                biome_at(SEED, x, z + 15) != b || biome_at(SEED, x + 15, z + 15) != b)
                continue;
            out.push_back(ColumnPos{cx, cz});
        }
    return out;
}

// A column of one block id everywhere, with pos set.
Column filled_column(ColumnPos c, BlockId b) {
    Column col;
    for (int cy = 0; cy < SECTIONS; ++cy) {
        col[static_cast<std::size_t>(cy)] = std::make_unique<Chunk>();
        col[static_cast<std::size_t>(cy)]->pos = ChunkPos{c.x, cy, c.z};
        col[static_cast<std::size_t>(cy)]->blocks.fill(b);
    }
    return col;
}

long count_id(const Column& col, BlockId b) {
    long n = 0;
    for (const auto& ch : col) n += static_cast<long>(std::count(ch->blocks.begin(), ch->blocks.end(), b));
    return n;
}

// No feature in `all` other than `self` lies within `d` blocks (Chebyshev, in x and z) of (x, z).
bool alone_near(const std::vector<Feature>& all, const Feature& self, int x, int z, int d) {
    for (const Feature& f : all) {
        if (f.x == self.x && f.z == self.z) continue;
        if (std::abs(f.x - x) <= d && std::abs(f.z - z) <= d) return false;
    }
    return true;
}

// Checks that every LOG / PINE_LOG run starts on GRASS, DIRT, or SNOW, and every TALL_GRASS /
// FLOWER stands on GRASS; counts trunks and plants.
void check_grounded(const Column& col, ColumnPos c, int& trunks, int& plants) {
    for (int lz = 0; lz < CHUNK; ++lz)
        for (int lx = 0; lx < CHUNK; ++lx) {
            const int x = c.x * CHUNK + lx, z = c.z * CHUNK + lz;
            for (int y = 1; y < WORLD_HEIGHT; ++y) {
                const BlockId b = block_at(col, c, x, y, z), below = block_at(col, c, x, y - 1, z);
                if ((b == blk::LOG || b == blk::PINE_LOG) && below != blk::LOG && below != blk::PINE_LOG) {
                    ++trunks;
                    CHECK(below == blk::GRASS || below == blk::DIRT || below == blk::SNOW);
                }
                if (b == blk::TALL_GRASS || b == blk::FLOWER) {
                    ++plants;
                    CHECK(below == blk::GRASS);
                }
            }
        }
}

}  // namespace

TEST(test_feature_biome_rules) {
    const std::vector<ColumnPos> desert = biome_columns(Biome::Desert, 64);
    const std::vector<ColumnPos> taiga = biome_columns(Biome::Taiga, 64);
    CHECK(desert.size() == 64);
    CHECK(taiga.size() == 64);
    long desert_logs = 0, taiga_oak = 0, taiga_pine = 0, cacti = 0;
    bool cactus_ok = true;
    for (ColumnPos c : desert) {
        const Column col = generate_column(SEED, c);
        for (int lz = 0; lz < CHUNK; ++lz)
            for (int lx = 0; lx < CHUNK; ++lx) {
                const int x = c.x * CHUNK + lx, z = c.z * CHUNK + lz;
                if (biome_at(SEED, x, z) != Biome::Desert) continue;
                for (int y = 1; y < WORLD_HEIGHT; ++y) {
                    const BlockId b = block_at(col, c, x, y, z);
                    desert_logs += (b == blk::LOG || b == blk::PINE_LOG) ? 1 : 0;
                    if (b != blk::CACTUS) continue;
                    ++cacti;
                    const BlockId below = block_at(col, c, x, y - 1, z);
                    cactus_ok = cactus_ok && (below == blk::SAND || below == blk::CACTUS);
                }
            }
    }
    for (ColumnPos c : taiga) {
        const Column col = generate_column(SEED, c);
        for (int lz = 0; lz < CHUNK; ++lz)
            for (int lx = 0; lx < CHUNK; ++lx) {
                const int x = c.x * CHUNK + lx, z = c.z * CHUNK + lz;
                if (biome_at(SEED, x, z) != Biome::Taiga) continue;
                for (int y = 1; y < WORLD_HEIGHT; ++y) {
                    const BlockId b = block_at(col, c, x, y, z);
                    taiga_oak += b == blk::LOG ? 1 : 0;
                    taiga_pine += b == blk::PINE_LOG ? 1 : 0;
                }
            }
    }
    CHECK(desert_logs == 0);
    CHECK(taiga_oak == 0);
    CHECK(taiga_pine > 0);
    CHECK(cacti > 0);
    CHECK(cactus_ok);
}

TEST(test_feature_border_invariance) {
    // A pine at local x = 15 whose cone reaches x + 1 and x + 2 (the next column), with no other
    // feature near those cells and lower ground there: the next column holds its PINE_LEAVES
    // whichever column is generated first.
    bool found = false;
    for (ColumnPos c : biome_columns(Biome::Taiga, 256)) {
        if (found) break;
        const std::vector<Feature> near = features_near(SEED, c, 3);
        for (const Feature& f : near) {
            if (f.type != FeatureType::PineTree || f.x != c.x * CHUNK + 15) continue;
            if (!alone_near(near, f, f.x + 1, f.z, 2) || !alone_near(near, f, f.x + 2, f.z, 2)) continue;
            const int low = f.y + f.size - 4;  // lowest cone layer (radius 2)
            if (terrain_height(SEED, f.x + 1, f.z) >= low || terrain_height(SEED, f.x + 2, f.z) >= low) continue;
            found = true;
            const ColumnPos east{c.x + 1, c.z};
            const Column east_alone = generate_column(SEED, east);
            CHECK(block_at(east_alone, east, f.x + 1, f.y + f.size - 2, f.z) == blk::PINE_LEAVES);  // radius 1
            CHECK(block_at(east_alone, east, f.x + 2, low, f.z) == blk::PINE_LEAVES);              // radius 2
            CHECK(block_at(east_alone, east, f.x + 2, f.y + f.size - 2, f.z) == blk::AIR);
            const Column west_alone = generate_column(SEED, c);
            for (int y = f.y; y < f.y + f.size; ++y) CHECK(block_at(west_alone, c, f.x, y, f.z) == blk::PINE_LOG);
            CHECK(block_at(west_alone, c, f.x, f.y + f.size, f.z) == blk::PINE_LEAVES);  // the tip
            {
                const Column w = generate_column(SEED, c);
                const Column e = generate_column(SEED, east);
                CHECK(same_column(e, east_alone));
                CHECK(same_column(w, west_alone));
            }
            {
                const Column e = generate_column(SEED, east);
                const Column w = generate_column(SEED, c);
                CHECK(same_column(e, east_alone));
                CHECK(same_column(w, west_alone));
            }
            break;
        }
    }
    CHECK(found);
}

TEST(test_plants_on_grass) {
    int trunks = 0, plants = 0;
    for (ColumnPos c : biome_columns(Biome::Meadow, 16)) {
        const Column col = generate_column(SEED, c);
        check_grounded(col, c, trunks, plants);
    }
    CHECK(plants > 0);
}

TEST(test_features_sorted_and_deterministic) {
    int total = 0;
    std::vector<ColumnPos> cols = biome_columns(Biome::Meadow, 2);
    const std::vector<ColumnPos> forest = biome_columns(Biome::Forest, 2);
    cols.insert(cols.end(), forest.begin(), forest.end());
    cols.push_back(ColumnPos{-1, -1});
    for (ColumnPos c : cols) {
        const std::vector<Feature> a = features_near(SEED, c, 3);
        const std::vector<Feature> b = features_near(SEED, c, 3);
        CHECK(a.size() == b.size());
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
            CHECK(a[i].x == b[i].x && a[i].y == b[i].y && a[i].z == b[i].z && a[i].type == b[i].type &&
                  a[i].size == b[i].size);
        for (std::size_t i = 1; i < a.size(); ++i)
            CHECK(std::make_tuple(a[i - 1].x, a[i - 1].z, a[i - 1].type) < std::make_tuple(a[i].x, a[i].z, a[i].type));
        for (const Feature& f : a) {
            ++total;
            CHECK(f.x >= c.x * CHUNK - 3 && f.x < c.x * CHUNK + CHUNK + 3);
            CHECK(f.z >= c.z * CHUNK - 3 && f.z < c.z * CHUNK + CHUNK + 3);
        }
    }
    CHECK(total > 0);
}

TEST(test_feature_trunk_grounded) {
    int trunks = 0, plants = 0;
    for (Biome b : {Biome::Forest, Biome::Taiga, Biome::Snowfield, Biome::Highlands})
        for (ColumnPos c : biome_columns(b, 6)) {
            const Column col = generate_column(SEED, c);
            check_grounded(col, c, trunks, plants);
        }
    CHECK(trunks > 0);
}

TEST(test_feature_negative_border) {
    // A tree at x = -1 (column x = -1, local 15) whose leaves reach x = 0 (column x = 0): both
    // columns hold its leaves, whichever is generated first.
    bool found = false;
    for (int cz = -256; cz < 256 && !found; ++cz) {
        const ColumnPos west{-1, cz}, east{0, cz};
        const Biome b = biome_at(SEED, -1, cz * CHUNK + 8);
        if (b == Biome::Ocean || b == Biome::Desert) continue;
        const std::vector<Feature> near = features_near(SEED, west, 3);
        for (const Feature& f : near) {
            if (!is_tree(f.type) || f.x != -1) continue;
            if (!alone_near(near, f, 0, f.z, 2) || !alone_near(near, f, -2, f.z, 2)) continue;
            // The leaf layer checked: oak y + 3 (radius 2), pine y + size - 2 (radius 1).
            const int ly = f.type == FeatureType::OakTree ? f.y + 3 : f.y + f.size - 2;
            const BlockId leaf = f.type == FeatureType::OakTree ? blk::LEAVES : blk::PINE_LEAVES;
            if (terrain_height(SEED, 0, f.z) >= ly || terrain_height(SEED, -2, f.z) >= ly) continue;
            found = true;
            const Column e1 = generate_column(SEED, east);
            const Column w1 = generate_column(SEED, west);
            const Column w2 = generate_column(SEED, west);
            const Column e2 = generate_column(SEED, east);
            CHECK(block_at(e1, east, 0, ly, f.z) == leaf);
            CHECK(block_at(w1, west, -2, ly, f.z) == leaf);
            CHECK(same_column(e1, e2));
            CHECK(same_column(w1, w2));
            break;
        }
    }
    CHECK(found);
}

TEST(test_cactus_height_range) {
    int cacti = 0;
    bool sizes_ok = true, seen[4] = {false, false, false, false};
    for (ColumnPos c : biome_columns(Biome::Desert, 64)) {
        for (const Feature& f : features_near(SEED, c, 0)) {
            if (f.type != FeatureType::Cactus) continue;
            sizes_ok = sizes_ok && f.size >= 1 && f.size <= 3;
            if (f.size <= 3) seen[f.size] = true;
        }
        const Column col = generate_column(SEED, c);
        for (int lz = 0; lz < CHUNK; ++lz)
            for (int lx = 0; lx < CHUNK; ++lx) {
                const int x = c.x * CHUNK + lx, z = c.z * CHUNK + lz;
                int run = 0;
                for (int y = 1; y < WORLD_HEIGHT; ++y) {
                    if (block_at(col, c, x, y, z) == blk::CACTUS) {
                        ++run;
                        continue;
                    }
                    if (run > 0) {
                        ++cacti;
                        CHECK(run >= 1 && run <= 3);
                    }
                    run = 0;
                }
            }
    }
    CHECK(cacti > 0);
    CHECK(sizes_ok);
    CHECK(seen[1] && seen[2] && seen[3]);
}

// ---- Coverage Checklist additions ----

TEST(test_feature_rule_table) {
    // Against the ground of a column generated without features, features_near(c, 0) holds exactly
    // the features the documented rules give: y = ground + 1; Forest oak hash2(seed ^ 0x7EE5) % 12
    // on GRASS; Meadow oak % 200, else flower hash2(seed ^ 0xF10E) % 24, else tall grass
    // hash2(seed ^ 0x6A55) % 4, on GRASS; Taiga pine hash2(seed ^ 0x714E) % 14 and Snowfield
    // pine % 60 on GRASS or SNOW; Desert cactus hash2(seed ^ 0xCAC7) % 150 on SAND at ground >= 64;
    // Highlands oak % 80 on GRASS with ground < 140; nothing in Ocean.
    int checked = 0, hits = 0;
    for (Biome biome : {Biome::Forest, Biome::Meadow, Biome::Taiga, Biome::Snowfield, Biome::Desert,
                        Biome::Highlands, Biome::Ocean})
        for (ColumnPos c : biome_columns(biome, 3)) {
            const Column col = generate_column(SEED, c, NO_FEATURES);
            std::map<std::pair<int, int>, Feature> got;
            for (const Feature& f : features_near(SEED, c, 0)) got.emplace(std::make_pair(f.x, f.z), f);
            for (int lz = 0; lz < CHUNK; ++lz)
                for (int lx = 0; lx < CHUNK; ++lx) {
                    const int x = c.x * CHUNK + lx, z = c.z * CHUNK + lz;
                    int h = WORLD_HEIGHT - 1;
                    while (h > 0 && (block_at(col, c, x, h, z) == blk::AIR || block_at(col, c, x, h, z) == blk::WATER))
                        --h;
                    const BlockId top = block_at(col, c, x, h, z);
                    const bool open = block_at(col, c, x, h + 1, z) == blk::AIR;
                    const bool grass = open && top == blk::GRASS, snowy = open && top == blk::SNOW;
                    const uint32_t oak = hash2(SEED ^ 0x7EE5u, x, z), flower = hash2(SEED ^ 0xF10Eu, x, z),
                                   tall = hash2(SEED ^ 0x6A55u, x, z), pine = hash2(SEED ^ 0x714Eu, x, z),
                                   cactus = hash2(SEED ^ 0xCAC7u, x, z);
                    bool want = false;
                    FeatureType type = FeatureType::OakTree;
                    switch (biome_at(SEED, x, z)) {
                        case Biome::Forest:
                            want = grass && oak % 12 == 0;
                            break;
                        case Biome::Meadow:
                            if (!grass) break;
                            want = true;
                            if (oak % 200 == 0) type = FeatureType::OakTree;
                            else if (flower % 24 == 0) type = FeatureType::Flower;
                            else if (tall % 4 == 0) type = FeatureType::TallGrass;
                            else want = false;
                            break;
                        case Biome::Taiga:
                            want = (grass || snowy) && pine % 14 == 0;
                            type = FeatureType::PineTree;
                            break;
                        case Biome::Snowfield:
                            want = (grass || snowy) && pine % 60 == 0;
                            type = FeatureType::PineTree;
                            break;
                        case Biome::Desert:
                            want = open && top == blk::SAND && h >= 64 && cactus % 150 == 0;
                            type = FeatureType::Cactus;
                            break;
                        case Biome::Highlands:
                            want = grass && h < 140 && oak % 80 == 0;
                            break;
                        default:
                            break;
                    }
                    ++checked;
                    const auto it = got.find({x, z});
                    CHECK(want == (it != got.end()));
                    if (!want || it == got.end()) continue;
                    ++hits;
                    const Feature& f = it->second;
                    CHECK(f.type == type);
                    CHECK(f.y == h + 1);
                    if (type == FeatureType::OakTree) CHECK(f.size == 5);
                    if (type == FeatureType::PineTree) CHECK(f.size >= 6 && f.size <= 8);
                    if (type == FeatureType::Cactus) CHECK(f.size >= 1 && f.size <= 3);
                    if (type == FeatureType::TallGrass || type == FeatureType::Flower) CHECK(f.size == 1);
                }
        }
    CHECK(checked > 0);
    CHECK(hits > 0);
}

TEST(test_feature_margin) {
    // margin selects features up to `margin` blocks outside the column; it is clamped to [0, 16].
    // Neighbor columns agree on the features they share.
    const std::vector<ColumnPos> cols = biome_columns(Biome::Meadow, 1);
    CHECK(!cols.empty());
    if (cols.empty()) return;
    const ColumnPos c = cols[0];
    const std::vector<Feature> m0 = features_near(SEED, c, 0), m3 = features_near(SEED, c, 3),
                               m16 = features_near(SEED, c, 16);
    for (const Feature& f : m0) {
        CHECK(f.x >= c.x * CHUNK && f.x < c.x * CHUNK + CHUNK);
        CHECK(f.z >= c.z * CHUNK && f.z < c.z * CHUNK + CHUNK);
    }
    std::size_t inside3 = 0;
    for (const Feature& f : m16) {
        CHECK(f.x >= c.x * CHUNK - 16 && f.x < c.x * CHUNK + CHUNK + 16);
        inside3 += (f.x >= c.x * CHUNK - 3 && f.x < c.x * CHUNK + 19 && f.z >= c.z * CHUNK - 3 &&
                    f.z < c.z * CHUNK + 19)
                       ? 1
                       : 0;
    }
    CHECK(inside3 == m3.size());
    CHECK(m3.size() > m0.size());
    CHECK(features_near(SEED, c, -5).size() == m0.size());
    CHECK(features_near(SEED, c, 40).size() == m16.size());

    // The east neighbor's own features equal c's margin-16 features that lie in it.
    const ColumnPos east{c.x + 1, c.z};
    std::size_t in_east = 0;
    for (const Feature& f : m16) in_east += column_at(f.x, f.z) == east ? 1 : 0;
    CHECK(in_east == features_near(SEED, east, 0).size());
}

TEST(test_feature_place_shapes) {
    // Each shape in an all-AIR column, away from its borders, at y = 100.
    const ColumnPos c{-2, 3};
    const int x = c.x * CHUNK + 8, z = c.z * CHUNK + 8;
    {
        Column col = filled_column(c, blk::AIR);
        place_feature(col, c, Feature{x, 100, z, FeatureType::OakTree, 5});
        CHECK(count_id(col, blk::LOG) == 5);
        CHECK(count_id(col, blk::LEAVES) == 58);  // 2 x (21 - 1) + 2 x 9
        for (int y = 100; y <= 104; ++y) CHECK(block_at(col, c, x, y, z) == blk::LOG);
        CHECK(block_at(col, c, x + 2, 103, z + 1) == blk::LEAVES);
        CHECK(block_at(col, c, x + 2, 103, z + 2) == blk::AIR);  // corner
        CHECK(block_at(col, c, x + 1, 106, z + 1) == blk::LEAVES);
        CHECK(block_at(col, c, x + 2, 105, z) == blk::AIR);
        CHECK(block_at(col, c, x, 107, z) == blk::AIR);
    }
    {
        Column col = filled_column(c, blk::AIR);
        place_feature(col, c, Feature{x, 100, z, FeatureType::PineTree, 6});
        CHECK(count_id(col, blk::PINE_LOG) == 6);
        CHECK(count_id(col, blk::PINE_LEAVES) == 57);  // 20 + 20 + 8 + 8 + 1
        CHECK(block_at(col, c, x, 106, z) == blk::PINE_LEAVES);    // tip
        CHECK(block_at(col, c, x + 2, 102, z + 1) == blk::PINE_LEAVES);
        CHECK(block_at(col, c, x + 2, 104, z) == blk::AIR);         // radius 1 layer
        CHECK(block_at(col, c, x + 1, 101, z) == blk::AIR);         // below the cone
        CHECK(block_at(col, c, x, 107, z) == blk::AIR);
    }
    {
        Column col = filled_column(c, blk::AIR);
        place_feature(col, c, Feature{x, 100, z, FeatureType::PineTree, 8});
        CHECK(count_id(col, blk::PINE_LOG) == 8);
        CHECK(block_at(col, c, x + 2, 104, z) == blk::PINE_LEAVES);
        CHECK(block_at(col, c, x + 2, 103, z) == blk::AIR);
    }
    {
        Column col = filled_column(c, blk::AIR);
        place_feature(col, c, Feature{x, 100, z, FeatureType::Cactus, 3});
        CHECK(count_id(col, blk::CACTUS) == 3);
        CHECK(block_at(col, c, x, 102, z) == blk::CACTUS);
        CHECK(block_at(col, c, x, 103, z) == blk::AIR);
    }
    {
        Column col = filled_column(c, blk::AIR);
        place_feature(col, c, Feature{x, 100, z, FeatureType::TallGrass, 1});
        place_feature(col, c, Feature{x + 1, 100, z, FeatureType::Flower, 1});
        CHECK(count_id(col, blk::TALL_GRASS) == 1 && count_id(col, blk::FLOWER) == 1);
        CHECK(block_at(col, c, x, 100, z) == blk::TALL_GRASS);
        CHECK(block_at(col, c, x + 1, 100, z) == blk::FLOWER);
    }
}

TEST(test_feature_place_overwrite_rules) {
    // Logs overwrite; leaves, cacti, and plants go only into AIR.
    const ColumnPos c{0, 0};
    Column col = filled_column(c, blk::STONE);
    place_feature(col, c, Feature{8, 100, 8, FeatureType::OakTree, 5});
    place_feature(col, c, Feature{3, 100, 3, FeatureType::PineTree, 7});
    place_feature(col, c, Feature{12, 100, 12, FeatureType::Cactus, 2});
    place_feature(col, c, Feature{1, 100, 1, FeatureType::Flower, 1});
    place_feature(col, c, Feature{14, 100, 1, FeatureType::TallGrass, 1});
    CHECK(count_id(col, blk::LOG) == 5);
    CHECK(count_id(col, blk::PINE_LOG) == 7);
    CHECK(count_id(col, blk::LEAVES) == 0);
    CHECK(count_id(col, blk::PINE_LEAVES) == 0);
    CHECK(count_id(col, blk::CACTUS) == 0);
    CHECK(count_id(col, blk::FLOWER) + count_id(col, blk::TALL_GRASS) == 0);
}

TEST(test_feature_place_clips_to_column) {
    // Trees at the column's borders and outside it write only the cells inside the column; cells
    // past the top of the world are skipped.
    const ColumnPos c{-1, -1};
    const int x0 = c.x * CHUNK, z0 = c.z * CHUNK;
    {
        Column col = filled_column(c, blk::AIR);
        place_feature(col, c, Feature{x0 - 2, 100, z0 + 8, FeatureType::OakTree, 5});  // 2 outside
        CHECK(count_id(col, blk::LOG) == 0);
        CHECK(count_id(col, blk::LEAVES) == 6);  // x0 at y 103-104, z0+7..z0+9
        CHECK(block_at(col, c, x0, 103, z0 + 9) == blk::LEAVES);
    }
    {
        Column col = filled_column(c, blk::AIR);
        place_feature(col, c, Feature{x0 - 3, 100, z0 + 8, FeatureType::OakTree, 5});  // out of reach
        place_feature(col, c, Feature{x0 + 18, 100, z0 + 8, FeatureType::PineTree, 6});
        CHECK(count_id(col, blk::LEAVES) == 0);
        CHECK(count_id(col, blk::PINE_LEAVES) == 0);
    }
    {
        Column col = filled_column(c, blk::AIR);
        place_feature(col, c, Feature{x0 + 15, 100, z0 + 15, FeatureType::OakTree, 5});  // corner
        CHECK(count_id(col, blk::LOG) == 5);
        CHECK(count_id(col, blk::LEAVES) == 2 * (8 - 1) + 2 * 4);  // 3x3 quarter minus corner and trunk
    }
    {
        Column col = filled_column(c, blk::AIR);
        place_feature(col, c, Feature{x0 + 8, 253, z0 + 8, FeatureType::PineTree, 8});
        CHECK(count_id(col, blk::PINE_LOG) == 3);  // y 253..255
    }
}

TEST(test_feature_over_ravine) {
    // Around a ravine on land, trees and plants stand only on uncarved ground.
    bool found = false;
    int trunks = 0, plants = 0;
    for (int gz = -16; gz < 16 && !found; ++gz)
        for (int gx = -16; gx < 16 && !found; ++gx)
            for (const Ravine& r : ravines_near(SEED, ColumnPos{gx * 4, gz * 4})) {
                if (floor_div(r.x0, 64) != gx || floor_div(r.z0, 64) != gz) continue;
                const Biome b = biome_at(SEED, r.x0, r.z0);
                if (b != Biome::Forest && b != Biome::Meadow && b != Biome::Taiga) continue;
                if (terrain_height(SEED, r.x0, r.z0) <= SEA_LEVEL + 4) continue;
                found = true;
                const ColumnPos o = column_at(r.x0, r.z0);
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dx = -2; dx <= 2; ++dx) {
                        const ColumnPos c{o.x + dx, o.z + dz};
                        const Column col = generate_column(SEED, c);
                        check_grounded(col, c, trunks, plants);
                    }
                break;
            }
    CHECK(found);
    CHECK(trunks + plants > 0);
}

TEST(test_feature_extreme_columns) {
    // Features at the corners of the supported range |pos| <= 2^27 - 2 (and a margin of 16) run
    // without overflow and stay within reach of the column.
    constexpr int M = (1 << 27) - 2;
    const ColumnPos corners[4] = {{M, M}, {-M, -M}, {M, -M}, {-M, M}};
    for (ColumnPos c : corners) {
        for (const Feature& f : features_near(SEED, c, 16)) {
            CHECK(std::abs(floor_div(f.x, CHUNK) - c.x) <= 1);
            CHECK(std::abs(floor_div(f.z, CHUNK) - c.z) <= 1);
            CHECK(f.y >= 1 && f.y < WORLD_HEIGHT);
        }
    }
}

TEST(test_cactus_sea_level_boundary) {
    // Boundary at the sea: a Desert cactus hit, hash2(seed ^ 0xCAC7, x, z) % 150 == 0, gives no
    // feature on sand at ground 63 (WATER above it) and a cactus at y 65 on ground 64.
    bool saw63 = false, saw64 = false;
    for (int z = -4096; z < 4096 && !(saw63 && saw64); ++z)
        for (int x = -4096; x < 4096 && !(saw63 && saw64); ++x) {
            if (hash2(SEED ^ 0xCAC7u, x, z) % 150 != 0 || biome_at(SEED, x, z) != Biome::Desert) continue;
            const int h = terrain_height(SEED, x, z);
            if (!(h == 63 && !saw63) && !(h == 64 && !saw64)) continue;
            bool cactus = false;
            for (const Feature& f : features_near(SEED, column_at(x, z), 0))
                cactus = cactus || (f.x == x && f.z == z && f.type == FeatureType::Cactus && f.y == 65);
            if (h == 63) {
                saw63 = true;
                CHECK(!cactus);
                bool any = false;
                for (const Feature& f : features_near(SEED, column_at(x, z), 0)) any = any || (f.x == x && f.z == z);
                CHECK(!any);
            } else {
                saw64 = true;
                CHECK(cactus);
            }
        }
    CHECK(saw63);
    CHECK(saw64);
}

TEST(test_highlands_oak_boundary) {
    // Boundary at y = 140: a Highlands oak hit, hash2(seed ^ 0x7EE5, x, z) % 80 == 0, gives an oak
    // on ground 139 and nothing on ground 140 (still GRASS there).
    bool saw139 = false, saw140 = false;
    for (int z = -4096; z < 4096 && !(saw139 && saw140); ++z)
        for (int x = -4096; x < 4096 && !(saw139 && saw140); ++x) {
            if (hash2(SEED ^ 0x7EE5u, x, z) % 80 != 0 || biome_at(SEED, x, z) != Biome::Highlands) continue;
            const int h = terrain_height(SEED, x, z);
            if (!(h == 139 && !saw139) && !(h == 140 && !saw140)) continue;
            bool oak = false, any = false;
            for (const Feature& f : features_near(SEED, column_at(x, z), 0)) {
                if (f.x != x || f.z != z) continue;
                any = true;
                oak = f.type == FeatureType::OakTree && f.y == h + 1;
            }
            if (h == 139) {
                saw139 = true;
                CHECK(oak);
            } else {
                saw140 = true;
                CHECK(!any);
            }
        }
    CHECK(saw139);
    CHECK(saw140);
}
