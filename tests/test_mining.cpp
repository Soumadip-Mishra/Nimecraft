// Tests for src/mining.*: break times, tool tiers, drops, and tool wear; and the BlockDef mining
// columns in src/block.*.
#include "block.hpp"
#include "block_entities.hpp"
#include "items.hpp"
#include "mining.hpp"
#include "player.hpp"
#include "test.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace {

ItemStack held(ItemId id) {
    return ItemStack{id, 1, 0};
}

const ItemStack HAND{};

bool drops(BlockId b, const ItemStack& h, ItemId id, int count) {
    const std::optional<ItemStack> d = block_drop(b, h, 0);
    return d && d->id == id && d->count == count;
}

}  // namespace

TEST(test_break_time_table) {
    CHECK_NEAR(break_time(blk::STONE, HAND), 7.5, 1e-5);
    CHECK(!block_drop(blk::STONE, HAND, 0));
    CHECK_NEAR(break_time(blk::STONE, held(itm::PICK_WOOD)), 1.125, 1e-5);
    CHECK(drops(blk::STONE, held(itm::PICK_WOOD), blk::STONE, 1));
    CHECK_NEAR(break_time(blk::IRON_ORE, held(itm::PICK_STONE)), 15.0, 1e-5);
    CHECK(!block_drop(blk::IRON_ORE, held(itm::PICK_STONE), 0));
    CHECK_NEAR(break_time(blk::IRON_ORE, held(itm::PICK_BRONZE)), 0.75, 1e-5);
    CHECK(drops(blk::IRON_ORE, held(itm::PICK_BRONZE), blk::IRON_ORE, 1));
    CHECK_NEAR(break_time(blk::DIRT, HAND), 0.75, 1e-5);
    CHECK_NEAR(break_time(blk::DIRT, held(itm::SHOVEL_WOOD)), 0.375, 1e-5);
    // Speed by tier {1, 2, 4, 5, 6, 8}: DIRT (base 0.75) with every shovel.
    CHECK_NEAR(break_time(blk::DIRT, held(itm::SHOVEL_STONE)), 0.1875, 1e-5);
    CHECK_NEAR(break_time(blk::DIRT, held(itm::SHOVEL_COPPER)), 0.15, 1e-5);
    CHECK_NEAR(break_time(blk::DIRT, held(itm::SHOVEL_BRONZE)), 0.125, 1e-5);
    CHECK_NEAR(break_time(blk::DIRT, held(itm::SHOVEL_IRON)), 0.09375, 1e-5);
    // A non-tool item counts as the hand.
    CHECK_NEAR(break_time(blk::DIRT, held(blk::STONE)), 0.75, 1e-5);
}

TEST(test_break_time_wrong_tool) {
    CHECK_NEAR(break_time(blk::LOG, held(itm::PICK_IRON)), 3.0, 1e-5);
    CHECK(drops(blk::LOG, held(itm::PICK_IRON), blk::LOG, 1));
    CHECK_NEAR(break_time(blk::LOG, held(itm::AXE_WOOD)), 1.5, 1e-5);
    // An iron axe is not a pick: STONE takes the no-tier time and drops nothing.
    CHECK_NEAR(break_time(blk::STONE, held(itm::AXE_IRON)), 7.5, 1e-5);
    CHECK(!block_drop(blk::STONE, held(itm::AXE_IRON), 0));
    // A sword is never the right tool.
    CHECK_NEAR(break_time(blk::PLANKS, held(itm::SWORD_IRON)), 3.0, 1e-5);
}

TEST(test_break_time_tier_boundary) {
    CHECK_NEAR(break_time(blk::COPPER_ORE, held(itm::PICK_WOOD)), 15.0, 1e-5);
    CHECK(!block_drop(blk::COPPER_ORE, held(itm::PICK_WOOD), 0));
    CHECK_NEAR(break_time(blk::COPPER_ORE, held(itm::PICK_STONE)), 1.125, 1e-5);
    CHECK(drops(blk::COPPER_ORE, held(itm::PICK_STONE), blk::COPPER_ORE, 1));
    // SILVER_ORE needs tier 5 exactly; QUARTZ tier 3.
    CHECK(!block_drop(blk::SILVER_ORE, held(itm::PICK_BRONZE), 0));
    CHECK(drops(blk::SILVER_ORE, held(itm::PICK_IRON), blk::SILVER_ORE, 1));
    CHECK(!block_drop(blk::QUARTZ, held(itm::PICK_STONE), 0));
    CHECK(block_drop(blk::QUARTZ, held(itm::PICK_COPPER), 0).has_value());
    CHECK(!block_drop(blk::TIN_ORE, held(itm::PICK_WOOD), 0));
    CHECK(drops(blk::TIN_ORE, held(itm::PICK_STONE), blk::TIN_ORE, 1));
    CHECK(!block_drop(blk::KILN, HAND, 0));
    CHECK(drops(blk::KILN, held(itm::PICK_WOOD), blk::KILN, 1));
}

TEST(test_break_time_unbreakable) {
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(break_time(blk::DEEPSTONE, HAND) == inf);
    CHECK(break_time(blk::DEEPSTONE, held(itm::PICK_IRON)) == inf);
    CHECK(break_time(blk::WATER, held(itm::SHOVEL_IRON)) == inf);
    CHECK(!block_drop(blk::DEEPSTONE, held(itm::PICK_IRON), 0));
    CHECK(!block_drop(blk::WATER, HAND, 0));
    CHECK(!block_drop(blk::AIR, HAND, 0));
    CHECK(!block_drop(999, HAND, 0));  // unknown ids read as AIR
}

TEST(test_drop_rules) {
    CHECK(drops(blk::GRASS, HAND, blk::DIRT, 1));
    CHECK(drops(blk::COAL_ORE, held(itm::PICK_WOOD), itm::COAL, 1));
    CHECK(!block_drop(blk::GLASS, HAND, 0));
    CHECK(!block_drop(blk::ICE, held(itm::PICK_IRON), 0));
    bool seen[4] = {};
    for (uint32_t r = 0; r < 1000; ++r) {
        const std::optional<ItemStack> d = block_drop(blk::QUARTZ, held(itm::PICK_COPPER), r);
        CHECK(d.has_value());
        if (!d) continue;
        CHECK(d->id == itm::QUARTZ_SHARD);
        CHECK(d->count >= 1 && d->count <= 3);
        if (d->count >= 1 && d->count <= 3) seen[d->count] = true;
    }
    CHECK(seen[1] && seen[2] && seen[3]);
    int sticks = 0;
    for (uint32_t r = 0; r < 10000; ++r) {
        const std::optional<ItemStack> d = block_drop(blk::LEAVES, HAND, r);
        if (!d) continue;
        CHECK(d->id == itm::STICK && d->count == 1);
        ++sticks;
    }
    CHECK(sticks >= 800 && sticks <= 1200);
    // Same rng, same result.
    for (uint32_t r : {0u, 7u, 123456u, 0xFFFFFFFFu}) {
        const auto a = block_drop(blk::QUARTZ, held(itm::PICK_IRON), r);
        const auto b = block_drop(blk::QUARTZ, held(itm::PICK_IRON), r);
        CHECK(a && b && a->count == b->count);
        CHECK(block_drop(blk::LEAVES, HAND, r).has_value() == block_drop(blk::LEAVES, HAND, r).has_value());
    }
}

// Values the user approved for blocks the task's table leaves out (2026-11-08): PINE_LEAVES as
// LEAVES; TALL_GRASS, FLOWER, TORCH, GLOWCAP instant; TALL_GRASS drops FIBER 10%; CACTUS 0.4;
// LANTERN 1.5 with a tier-1 pick; the rest drop themselves.
TEST(test_drop_rules_user_values) {
    CHECK(break_time(blk::TALL_GRASS, HAND) == 0.f);
    CHECK(break_time(blk::FLOWER, HAND) == 0.f);
    CHECK(break_time(blk::TORCH, HAND) == 0.f);
    CHECK(break_time(blk::GLOWCAP, HAND) == 0.f);
    CHECK(drops(blk::FLOWER, HAND, blk::FLOWER, 1));
    CHECK(drops(blk::TORCH, HAND, blk::TORCH, 1));
    CHECK(drops(blk::GLOWCAP, HAND, blk::GLOWCAP, 1));
    CHECK_NEAR(break_time(blk::CACTUS, HAND), 0.6, 1e-5);
    CHECK(drops(blk::CACTUS, HAND, blk::CACTUS, 1));
    CHECK_NEAR(break_time(blk::LANTERN, HAND), 7.5, 1e-5);
    CHECK(!block_drop(blk::LANTERN, HAND, 0));
    CHECK(drops(blk::LANTERN, held(itm::PICK_WOOD), blk::LANTERN, 1));
    CHECK_NEAR(break_time(blk::PINE_LEAVES, HAND), 0.3, 1e-5);
    int fiber = 0, pine_sticks = 0;
    for (uint32_t r = 0; r < 10000; ++r) {
        if (const auto d = block_drop(blk::TALL_GRASS, HAND, r)) {
            CHECK(d->id == itm::FIBER);
            ++fiber;
        }
        if (const auto d = block_drop(blk::PINE_LEAVES, HAND, r)) {
            CHECK(d->id == itm::STICK);
            ++pine_sticks;
        }
    }
    CHECK(fiber >= 800 && fiber <= 1200);
    CHECK(pine_sticks >= 800 && pine_sticks <= 1200);
}

// The task's hardness table and min tiers.
TEST(test_block_hardness_table) {
    struct Row {
        BlockId b;
        float hardness;
        uint8_t min_tier;
    };
    const Row rows[] = {
        {blk::DIRT, 0.5f, 0},       {blk::SAND, 0.5f, 0},      {blk::GRAVEL, 0.5f, 0},
        {blk::SNOW, 0.5f, 0},       {blk::GRASS, 0.6f, 0},     {blk::CLAY, 0.6f, 0},
        {blk::LOG, 2.f, 0},         {blk::PINE_LOG, 2.f, 0},   {blk::PLANKS, 2.f, 0},
        {blk::WORKBENCH, 2.f, 0},   {blk::CRATE, 2.f, 0},      {blk::LEAVES, 0.2f, 0},
        {blk::STONE, 1.5f, 1},      {blk::SANDSTONE, 1.5f, 1}, {blk::KILN, 3.5f, 1},
        {blk::COAL_ORE, 3.f, 1},    {blk::COPPER_ORE, 3.f, 2}, {blk::TIN_ORE, 3.f, 2},
        {blk::IRON_ORE, 3.f, 4},    {blk::SILVER_ORE, 3.f, 5}, {blk::QUARTZ, 3.f, 3},
        {blk::GLASS, 0.3f, 0},      {blk::ICE, 0.5f, 0},
    };
    for (const Row& r : rows) {
        CHECK(block_def(r.b).hardness == r.hardness);
        CHECK(block_def(r.b).min_tier == r.min_tier);
        if (r.min_tier > 0) CHECK(block_def(r.b).tool == ToolKind::Pick);
    }
    // Every breakable block has a finite hardness >= 0 and drops at most its max.
    for (BlockId b = 1; b < blk::COUNT; ++b) {
        const BlockDef& d = block_def(b);
        CHECK(d.hardness >= 0.f);
        CHECK(d.drop_min <= d.drop_max);
        CHECK(d.drop_chance <= 100);
    }
}

TEST(test_tool_wears) {
    ItemStack pick = held(itm::PICK_WOOD);
    for (int i = 0; i < 59; ++i) wear_tool(pick);
    CHECK(pick.damage == 59);
    CHECK(pick.id == itm::PICK_WOOD && pick.count == 1);
    wear_tool(pick);
    CHECK(pick.empty());
    CHECK(pick.id == 0 && pick.damage == 0);
    ItemStack dirt{blk::DIRT, 5, 0};
    wear_tool(dirt);
    CHECK(dirt.id == blk::DIRT && dirt.count == 5 && dirt.damage == 0);
    ItemStack helmet = held(itm::IRON_HELMET);  // armor does not wear by mining
    wear_tool(helmet);
    CHECK(helmet.damage == 0 && helmet.count == 1);
    ItemStack none{};
    wear_tool(none);
    CHECK(none.empty());
    ItemStack sword = held(itm::SWORD_SILVER);
    for (int i = 0; i < 250; ++i) wear_tool(sword);
    CHECK(sword.empty());
}

// A damaged tool mines at its tier like a new one.
TEST(test_break_time_damaged_tool) {
    ItemStack pick{itm::PICK_STONE, 1, 129};
    CHECK_NEAR(break_time(blk::STONE, pick), 0.5625, 1e-5);
}

// What breaking a block gives in each mode (Coverage row 13): Survival drops the block entity's
// contents, then the block drop, and wears the held tool on blocks with hardness > 0; Creative gives
// nothing and wears nothing.
TEST(test_break_drops_modes) {
    CrateState crate;
    crate.slots[0] = ItemStack{blk::DIRT, 5, 0};
    crate.slots[9] = ItemStack{itm::COAL, 2, 0};
    const BlockEntity be{crate};
    ItemStack axe = held(itm::AXE_WOOD);
    const std::vector<ItemStack> got = break_drops(blk::CRATE, &be, axe, GameMode::Survival, 0);
    CHECK(got.size() == 3);
    CHECK(got[0].id == blk::DIRT && got[0].count == 5);
    CHECK(got[1].id == itm::COAL && got[1].count == 2);
    CHECK(got[2].id == blk::CRATE && got[2].count == 1);
    CHECK(axe.damage == 1);

    ItemStack axe2 = held(itm::AXE_WOOD);
    CHECK(break_drops(blk::CRATE, &be, axe2, GameMode::Creative, 0).empty());
    CHECK(axe2.damage == 0);

    // Instant blocks cost no durability; a pick below STONE's tier still wears but drops nothing.
    ItemStack pick = held(itm::PICK_WOOD);
    const std::vector<ItemStack> torch = break_drops(blk::TORCH, nullptr, pick, GameMode::Survival, 0);
    CHECK(torch.size() == 1 && torch[0].id == blk::TORCH);
    CHECK(pick.damage == 0);
    ItemStack shovel = held(itm::SHOVEL_STONE);
    CHECK(break_drops(blk::IRON_ORE, nullptr, shovel, GameMode::Survival, 0).empty());
    CHECK(shovel.damage == 1);
    // Unbreakable blocks give nothing and wear nothing.
    ItemStack iron = held(itm::PICK_IRON);
    CHECK(break_drops(blk::DEEPSTONE, nullptr, iron, GameMode::Survival, 0).empty());
    CHECK(iron.damage == 0);
}
