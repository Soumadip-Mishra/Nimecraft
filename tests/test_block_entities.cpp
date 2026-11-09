// Tests for src/block_entities.*: kiln smelting and fuel, crate and cairn contents; and the
// World's block entity storage (src/world.*).
#include "block.hpp"
#include "block_entities.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "items.hpp"
#include "test.hpp"
#include "world.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace {

ItemStack st(ItemId id, int count) {
    return ItemStack{id, static_cast<uint8_t>(count), 0};
}

KilnState kiln(ItemStack in0, ItemStack fuel) {
    KilnState k;
    k.in[0] = in0;
    k.fuel = fuel;
    return k;
}

World air_world() {
    World w(1);
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz) w.insert_column(ColumnPos{cx, cz}, Column{});
    return w;
}

}  // namespace

TEST(test_kiln_smelts_ore) {
    KilnState a = kiln(st(blk::COPPER_ORE, 1), st(itm::COAL, 1));
    kiln_tick(a, 9.9f);
    CHECK(a.out.empty());
    CHECK(a.in[0].count == 1);
    CHECK(a.fuel.empty());  // the coal was lit
    CHECK_NEAR(a.burn_left, 70.1, 1e-4);
    CHECK(a.burn_total == 80.f);
    CHECK_NEAR(a.progress, 9.9, 1e-4);
    kiln_tick(a, 0.1f);
    CHECK(a.out.id == itm::COPPER_INGOT && a.out.count == 1);
    CHECK(a.in[0].empty());
    CHECK(a.progress == 0.f);
    KilnState b = kiln(st(blk::COPPER_ORE, 1), st(itm::COAL, 1));
    kiln_tick(b, 10.f);
    CHECK(b.out.id == itm::COPPER_INGOT && b.out.count == 1);
    // The other single-input recipes.
    const std::pair<ItemId, ItemId> table[] = {
        {blk::TIN_ORE, itm::TIN_INGOT},  {blk::IRON_ORE, itm::IRON_INGOT},       {blk::SILVER_ORE, itm::SILVER_INGOT},
        {blk::SAND, blk::GLASS},          {itm::RAW_MEAT, itm::COOKED_MEAT}, {itm::SMALL_MEAT, itm::COOKED_SMALL_MEAT},
    };
    for (const auto& [in, out] : table) {
        KilnState k = kiln(st(in, 1), st(itm::COAL, 1));
        kiln_tick(k, 10.f);
        CHECK(k.out.id == out && k.out.count == 1);
    }
    // An input in the second slot smelts too.
    KilnState second;
    second.in[1] = st(blk::IRON_ORE, 2);
    second.fuel = st(itm::COAL, 1);
    kiln_tick(second, 20.f);
    CHECK(second.out.id == itm::IRON_INGOT && second.out.count == 2);
    CHECK(second.in[1].empty());
}

TEST(test_kiln_fuel_lasts) {
    KilnState k = kiln(st(blk::IRON_ORE, 9), st(itm::COAL, 1));
    kiln_tick(k, 80.f);
    CHECK(k.out.count == 8);
    CHECK(k.burn_left == 0.f);
    kiln_tick(k, 10.f);  // the 9th needs new fuel
    CHECK(k.out.count == 8);
    CHECK(k.in[0].count == 1);
    k.fuel = st(blk::PLANKS, 2);  // 7.5 s each: the smelt lights both, 5 s of the second remain
    kiln_tick(k, 10.f);
    CHECK(k.out.count == 9);
    CHECK(k.fuel.empty());
    CHECK_NEAR(k.burn_left, 5.0, 1e-4);
    CHECK(k.burn_total == 7.5f);
}

TEST(test_kiln_large_dt) {
    KilnState k = kiln(st(blk::COPPER_ORE, 10), st(itm::COAL, 2));
    kiln_tick(k, 100.f);
    CHECK(k.out.id == itm::COPPER_INGOT && k.out.count == 10);
    CHECK(k.in[0].empty());
    CHECK(k.fuel.empty());
    CHECK(k.burn_left == 60.f);
    // dt = 100 once equals 6000 steps of 1/60 within float rounding.
    KilnState s = kiln(st(blk::COPPER_ORE, 10), st(itm::COAL, 2));
    for (int i = 0; i < 6000; ++i) kiln_tick(s, 1.f / 60.f);
    CHECK(s.out.count == 10);
    CHECK_NEAR(s.burn_left, 60.0, 0.01);
}

TEST(test_kiln_stops_when_output_full) {
    KilnState k = kiln(st(blk::COPPER_ORE, 5), st(itm::COAL, 1));
    k.out = st(itm::COPPER_INGOT, 64);
    kiln_tick(k, 30.f);
    CHECK(k.out.count == 64);
    CHECK(k.in[0].count == 5);
    CHECK(k.fuel.count == 1);
    CHECK(k.burn_left == 0.f);
    CHECK(k.progress == 0.f);
    // 63 leaves room for exactly one more.
    k.out = st(itm::COPPER_INGOT, 63);
    kiln_tick(k, 30.f);
    CHECK(k.out.count == 64);
    CHECK(k.in[0].count == 4);
    CHECK_NEAR(k.burn_left, 70.0, 1e-4);  // stopped burning once the output filled
}

// Taking the input out mid-smelt resets progress: the next smelt needs the full 10 s again.
TEST(test_kiln_progress_resets_without_input) {
    KilnState k = kiln(st(blk::IRON_ORE, 1), st(itm::COAL, 1));
    kiln_tick(k, 6.f);
    CHECK_NEAR(k.progress, 6.0, 1e-4);
    k.in[0] = ItemStack{};
    kiln_tick(k, 1.f);
    CHECK(k.progress == 0.f);
    k.in[0] = st(blk::IRON_ORE, 1);
    kiln_tick(k, 9.f);
    CHECK(k.out.empty());
    kiln_tick(k, 1.f);
    CHECK(k.out.id == itm::IRON_INGOT);
}

TEST(test_kiln_output_other_item) {
    KilnState k = kiln(st(blk::COPPER_ORE, 1), st(itm::COAL, 1));
    k.out = st(itm::IRON_INGOT, 1);
    kiln_tick(k, 20.f);
    CHECK(k.out.id == itm::IRON_INGOT && k.out.count == 1);
    CHECK(k.in[0].count == 1);
    CHECK(k.fuel.count == 1);
}

TEST(test_kiln_no_fuel_waste_without_input) {
    KilnState k;
    k.fuel = st(itm::COAL, 3);
    kiln_tick(k, 100.f);
    CHECK(k.fuel.count == 3);
    CHECK(k.burn_left == 0.f);
    // A non-smeltable input does not light the fuel either.
    k.in[0] = st(blk::DIRT, 5);
    kiln_tick(k, 100.f);
    CHECK(k.fuel.count == 3);
    CHECK(k.in[0].count == 5);
    // Fuel already burning pauses (does not burn down) while nothing can smelt.
    KilnState p = kiln(st(blk::SAND, 1), st(itm::COAL, 1));
    kiln_tick(p, 10.f);
    CHECK_NEAR(p.burn_left, 70.0, 1e-4);
    kiln_tick(p, 50.f);
    CHECK_NEAR(p.burn_left, 70.0, 1e-4);
}

TEST(test_kiln_non_fuel) {
    KilnState k = kiln(st(blk::COPPER_ORE, 1), st(blk::DIRT, 4));
    kiln_tick(k, 20.f);
    CHECK(k.out.empty());
    CHECK(k.fuel.count == 4);
    CHECK(k.burn_left == 0.f);
    CHECK(fuel_seconds(itm::COAL) == 80.f);
    CHECK(fuel_seconds(blk::LOG) == 15.f);
    CHECK(fuel_seconds(blk::PINE_LOG) == 15.f);
    CHECK(fuel_seconds(blk::PLANKS) == 7.5f);
    CHECK(fuel_seconds(itm::STICK) == 2.5f);
    CHECK(fuel_seconds(blk::DIRT) == 0.f);
    CHECK(fuel_seconds(0) == 0.f);
    CHECK(fuel_seconds(9999) == 0.f);
    // Zero, negative, and NaN dt change nothing.
    KilnState z = kiln(st(blk::COPPER_ORE, 1), st(itm::COAL, 1));
    for (float dt : {0.f, -5.f, std::numeric_limits<float>::quiet_NaN()}) kiln_tick(z, dt);
    CHECK(z.fuel.count == 1);
    CHECK(z.progress == 0.f);
}

TEST(test_kiln_bronze_alloy) {
    KilnState k;
    k.in[0] = st(itm::COPPER_INGOT, 3);
    k.in[1] = st(itm::TIN_INGOT, 1);
    k.fuel = st(itm::COAL, 1);
    kiln_tick(k, 10.f);
    CHECK(k.out.id == itm::BRONZE_INGOT && k.out.count == 4);
    CHECK(k.in[0].empty() && k.in[1].empty());
    KilnState few;
    few.in[0] = st(itm::COPPER_INGOT, 2);
    few.in[1] = st(itm::TIN_INGOT, 1);
    few.fuel = st(itm::COAL, 1);
    kiln_tick(few, 10.f);
    CHECK(few.out.empty());
    CHECK(few.fuel.count == 1);
    KilnState swapped;
    swapped.in[0] = st(itm::TIN_INGOT, 2);
    swapped.in[1] = st(itm::COPPER_INGOT, 6);
    swapped.fuel = st(itm::COAL, 1);
    kiln_tick(swapped, 20.f);
    CHECK(swapped.out.id == itm::BRONZE_INGOT && swapped.out.count == 8);
    CHECK(swapped.in[0].empty() && swapped.in[1].empty());
    // 61 bronze has no room for 4 more.
    KilnState full;
    full.in[0] = st(itm::COPPER_INGOT, 3);
    full.in[1] = st(itm::TIN_INGOT, 1);
    full.fuel = st(itm::COAL, 1);
    full.out = st(itm::BRONZE_INGOT, 61);
    kiln_tick(full, 10.f);
    CHECK(full.out.count == 61);
}

TEST(test_smelt_recipes_table) {
    const std::vector<SmeltRecipe>& r = smelt_recipes();
    CHECK(r.size() == 8);
    for (const SmeltRecipe& s : r) {
        CHECK(!s.inputs[0].empty());
        CHECK(s.output.count >= 1);
        CHECK(item_def(s.output.id).max_stack > 0);
    }
}

TEST(test_crate_contents_dropped) {
    CrateState c;
    for (int i = 0; i < 27; ++i) c.slots[static_cast<std::size_t>(i)] = st(static_cast<ItemId>(itm::STICK + i), 1);
    const std::vector<ItemStack> got = block_entity_contents(BlockEntity{c});
    CHECK(got.size() == 27);
    for (std::size_t i = 0; i < got.size(); ++i) CHECK(got[i].id == itm::STICK + i);
    CHECK(block_entity_contents(BlockEntity{CrateState{}}).empty());
    KilnState k = kiln(st(blk::SAND, 2), st(itm::COAL, 1));
    k.out = st(blk::GLASS, 3);
    const std::vector<ItemStack> kg = block_entity_contents(BlockEntity{k});
    CHECK(kg.size() == 3);
    CairnState cairn{{st(blk::DIRT, 1), ItemStack{}, st(itm::HIDE, 2)}};
    CHECK(block_entity_contents(BlockEntity{cairn}).size() == 2);
}

TEST(test_block_entity_moves_with_column) {
    World w = air_world();
    w.set(-3, 70, -5, blk::KILN);
    KilnState k = kiln(st(blk::IRON_ORE, 4), st(itm::COAL, 2));
    k.progress = 4.5f;
    w.set_block_entity(BlockPos{-3, 70, -5}, k);
    Column col = w.remove_column(ColumnPos{-1, -1});
    CHECK(w.block_entity(BlockPos{-3, 70, -5}) == nullptr);
    w.insert_column(ColumnPos{-1, -1}, std::move(col));
    const BlockEntity* be = w.block_entity(BlockPos{-3, 70, -5});
    CHECK(be && std::holds_alternative<KilnState>(*be));
    if (be && std::holds_alternative<KilnState>(*be)) {
        CHECK(std::get<KilnState>(*be).progress == 4.5f);
        CHECK(std::get<KilnState>(*be).in[0].count == 4);
    }
}

TEST(test_world_block_entity_api) {
    World w = air_world();
    const BlockPos p{15, 255, -16};  // column border, top of the world
    CHECK(w.block_entity(p) == nullptr);
    w.set(p.x, p.y, p.z, blk::CRATE);
    CrateState c;
    c.slots[3] = st(blk::DIRT, 9);
    w.set_block_entity(p, c);
    BlockEntity* be = w.block_entity(p);
    CHECK(be && std::holds_alternative<CrateState>(*be));
    std::get<CrateState>(*be).slots[4] = st(blk::SAND, 1);  // edited in place
    CHECK(std::get<CrateState>(*w.block_entity(p)).slots[4].count == 1);
    CHECK(w.chunk(chunk_of(p.x, p.y, p.z))->modified);
    const std::vector<BlockPos> all = w.block_entity_positions();
    CHECK(all.size() == 1 && all[0] == p);
    w.remove_block_entity(p);
    CHECK(w.block_entity(p) == nullptr);
    CHECK(w.block_entity_positions().empty());
    w.remove_block_entity(p);  // removing nothing is fine

    // Unloaded columns and y outside the world hold nothing.
    w.set_block_entity(BlockPos{100, 70, 100}, CrateState{});
    CHECK(w.block_entity(BlockPos{100, 70, 100}) == nullptr);
    w.set_block_entity(BlockPos{0, -1, 0}, CrateState{});
    w.set_block_entity(BlockPos{0, 256, 0}, CrateState{});
    CHECK(w.block_entity(BlockPos{0, -1, 0}) == nullptr);
    CHECK(w.block_entity(BlockPos{0, 256, 0}) == nullptr);
    CHECK(w.block_entity_positions().empty());

    // Changing the block removes its entity; setting the same block keeps it.
    w.set(1, 70, 1, blk::KILN);
    w.set_block_entity(BlockPos{1, 70, 1}, KilnState{});
    w.set(1, 70, 1, blk::KILN);
    CHECK(w.block_entity(BlockPos{1, 70, 1}) != nullptr);
    w.set(1, 70, 1, blk::AIR);
    CHECK(w.block_entity(BlockPos{1, 70, 1}) == nullptr);
    const World& cw = w;
    CHECK(cw.block_entity(BlockPos{1, 70, 1}) == nullptr);
}
