// Tests for src/recipes.*: the recipe table, station detection, available recipes, crafting.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "inventory.hpp"
#include "items.hpp"
#include "recipes.hpp"
#include "test.hpp"
#include "world.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace {

ItemStack stack(ItemId id, int count) {
    return ItemStack{id, static_cast<uint8_t>(count), 0};
}

// Columns (-1..1, -1..1) of AIR.
World air_world() {
    World w(1);
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz) w.insert_column(ColumnPos{cx, cz}, Column{});
    return w;
}

bool same(const Inventory& a, const Inventory& b) {
    for (std::size_t i = 0; i < a.slots.size(); ++i)
        if (a.slots[i].id != b.slots[i].id || a.slots[i].count != b.slots[i].count ||
            a.slots[i].damage != b.slots[i].damage)
            return false;
    return true;
}

const Recipe* find(ItemId output, Station st) {
    for (const Recipe& r : recipes())
        if (r.output.id == output && r.station == st) return &r;
    return nullptr;
}

const Recipe* find_input(ItemId output, ItemId first_input) {
    for (const Recipe& r : recipes())
        if (r.output.id == output && r.inputs[0].id == first_input) return &r;
    return nullptr;
}

bool lists(const std::vector<const Recipe*>& v, ItemId output) {
    return std::any_of(v.begin(), v.end(), [output](const Recipe* r) { return r->output.id == output; });
}

StationSet bench() {
    StationSet s;
    s.set(static_cast<std::size_t>(Station::Workbench));
    return s;
}

}  // namespace

TEST(test_available_filters_station) {
    Inventory inv;
    inv.slots[0] = stack(blk::PLANKS, 8);
    inv.slots[1] = stack(itm::STICK, 8);
    const std::vector<const Recipe*> hand = available(inv, StationSet{});
    CHECK(lists(hand, blk::WORKBENCH));
    CHECK(lists(hand, itm::STICK));
    CHECK(!lists(hand, itm::PICK_WOOD));
    CHECK(!lists(hand, blk::CRATE));
    const std::vector<const Recipe*> at_bench = available(inv, bench());
    CHECK(lists(at_bench, itm::PICK_WOOD));
    CHECK(lists(at_bench, itm::AXE_WOOD));
    CHECK(lists(at_bench, itm::SHOVEL_WOOD));
    CHECK(lists(at_bench, itm::SWORD_WOOD));
    CHECK(lists(at_bench, blk::CRATE));
    CHECK(lists(at_bench, blk::WORKBENCH));  // hand recipes stay available at a station
    // The kiln station alone unlocks no workbench recipe.
    StationSet kiln;
    kiln.set(static_cast<std::size_t>(Station::Kiln));
    CHECK(!lists(available(inv, kiln), itm::PICK_WOOD));
}

TEST(test_available_requires_inputs) {
    Inventory inv;
    CHECK(available(inv, bench()).empty());
    CHECK(available(inv, StationSet{}).empty());
    inv.slots[0] = stack(blk::LOG, 1);
    const std::vector<const Recipe*> v = available(inv, StationSet{});
    CHECK(v.size() == 1);
    CHECK(v[0]->output.id == blk::PLANKS && v[0]->output.count == 4);
    CHECK(v[0]->inputs.size() == 1 && v[0]->inputs[0].id == blk::LOG);
    // Exactly enough: 3 PLANKS + 2 STICK at a bench lists the pick and the axe, 2 + 2 does not.
    Inventory exact;
    exact.slots[0] = stack(blk::PLANKS, 3);
    exact.slots[1] = stack(itm::STICK, 2);
    CHECK(lists(available(exact, bench()), itm::PICK_WOOD));
    exact.slots[0] = stack(blk::PLANKS, 2);
    CHECK(!lists(available(exact, bench()), itm::PICK_WOOD));
}

TEST(test_craft_consumes_exactly) {
    Inventory inv;
    inv.slots[0] = stack(blk::PLANKS, 3);
    inv.slots[1] = stack(itm::STICK, 2);
    const Recipe* pick = find(itm::PICK_WOOD, Station::Workbench);
    CHECK(pick != nullptr);
    if (!pick) return;
    CHECK(craft(inv, *pick));
    CHECK(inv.count(blk::PLANKS) == 0);
    CHECK(inv.count(itm::STICK) == 0);
    CHECK(inv.count(itm::PICK_WOOD) == 1);
}

TEST(test_craft_inputs_across_slots) {
    Inventory inv;
    inv.slots[4] = stack(blk::PLANKS, 1);
    inv.slots[30] = stack(blk::PLANKS, 2);
    inv.slots[12] = stack(itm::STICK, 5);
    const Recipe* pick = find(itm::PICK_WOOD, Station::Workbench);
    CHECK(pick && craft(inv, *pick));
    CHECK(inv.count(blk::PLANKS) == 0);
    CHECK(inv.count(itm::STICK) == 3);
    CHECK(inv.count(itm::PICK_WOOD) == 1);
}

TEST(test_craft_atomic_when_missing) {
    Inventory inv;
    inv.slots[0] = stack(blk::PLANKS, 2);
    inv.slots[1] = stack(itm::STICK, 2);
    const Inventory before = inv;
    const Recipe* pick = find(itm::PICK_WOOD, Station::Workbench);
    CHECK(pick && !craft(inv, *pick));
    CHECK(same(inv, before));
}

TEST(test_craft_atomic_when_full) {
    // Every slot full: 35 STONE stacks and one full PLANKS stack. 4 PLANKS -> WORKBENCH leaves the
    // PLANKS slot non-empty, so the WORKBENCH has nowhere to go.
    Inventory inv;
    for (auto& s : inv.slots) s = stack(blk::STONE, 64);
    inv.slots[2] = stack(blk::PLANKS, 64);
    const Inventory before = inv;
    const Recipe* wb = find(blk::WORKBENCH, Station::None);
    CHECK(wb != nullptr);
    if (!wb) return;
    CHECK(!craft(inv, *wb));
    CHECK(same(inv, before));
}

TEST(test_craft_output_merges) {
    Inventory inv;
    for (auto& s : inv.slots) s = stack(blk::STONE, 64);
    inv.slots[3] = stack(blk::PLANKS, 60);
    inv.slots[7] = stack(blk::LOG, 1);
    const Recipe* r = find_input(blk::PLANKS, blk::LOG);
    CHECK(r && craft(inv, *r));
    CHECK(inv.slots[3].count == 64);
    CHECK(inv.slots[7].empty());
    CHECK(inv.count(blk::LOG) == 0);
    // 62 PLANKS: 2 merge, the other 2 go into the freed LOG slot.
    Inventory two;
    for (auto& s : two.slots) s = stack(blk::STONE, 64);
    two.slots[3] = stack(blk::PLANKS, 62);
    two.slots[7] = stack(blk::LOG, 1);
    CHECK(r && craft(two, *r));
    CHECK(two.count(blk::PLANKS) == 66);
}

TEST(test_stations_near_radius) {
    World w = air_world();
    const Vector3 pos{0.5f, 70.5f, 0.5f};
    w.set(3, 70, 0, blk::WORKBENCH);  // center (3.5, 70.5, 0.5): exactly 3.0 away
    CHECK(stations_near(w, pos).test(static_cast<std::size_t>(Station::Workbench)));
    CHECK(!stations_near(w, pos).test(static_cast<std::size_t>(Station::Kiln)));
    CHECK(!stations_near(w, Vector3{0.f, 70.5f, 0.5f}).test(static_cast<std::size_t>(Station::Workbench)));  // 3.5
    CHECK(!stations_near(w, pos, 2.9f).test(static_cast<std::size_t>(Station::Workbench)));

    World below = air_world();
    below.set(0, 67, 0, blk::KILN);  // straight below, 3.0
    CHECK(stations_near(below, pos).test(static_cast<std::size_t>(Station::Kiln)));
    CHECK(!stations_near(below, pos).test(static_cast<std::size_t>(Station::Workbench)));
    // Negative coordinates across a column border.
    World neg = air_world();
    neg.set(-2, 70, -1, blk::WORKBENCH);  // center (-1.5, 70.5, -0.5): sqrt(4 + 1) from pos
    CHECK(stations_near(neg, pos).test(static_cast<std::size_t>(Station::Workbench)));

    // A station in an unloaded column is ignored without a crash.
    World edge(1);
    edge.insert_column(ColumnPos{0, 0}, Column{});
    CHECK(stations_near(edge, Vector3{0.2f, 70.5f, 0.2f}).none());
    // StationSet::None bit is never reported.
    CHECK(!stations_near(w, pos).test(static_cast<std::size_t>(Station::None)));
}

TEST(test_recipes_table_valid) {
    const std::vector<Recipe>& all = recipes();
    CHECK(!all.empty());
    for (const Recipe& r : all) {
        CHECK(!r.inputs.empty());
        for (const ItemStack& in : r.inputs) {
            CHECK(in.count >= 1);
            CHECK(item_def(in.id).max_stack > 0);
        }
        CHECK(r.output.count >= 1);
        CHECK(r.output.count <= item_def(r.output.id).max_stack);
        CHECK(item_def(r.output.id).max_stack > 0);
    }
    // No duplicate recipe: the task's table gives the pick and the axe the same inputs at the
    // workbench (3 head + 2 STICK), which the recipe list tells apart by output, so a recipe is
    // a duplicate only when its inputs, station, and output all match another one.
    for (std::size_t a = 0; a < all.size(); ++a)
        for (std::size_t b = a + 1; b < all.size(); ++b) {
            const Recipe& x = all[a];
            const Recipe& y = all[b];
            bool same_inputs = x.inputs.size() == y.inputs.size();
            for (std::size_t i = 0; same_inputs && i < x.inputs.size(); ++i)
                same_inputs = x.inputs[i].id == y.inputs[i].id && x.inputs[i].count == y.inputs[i].count;
            CHECK(!(same_inputs && x.station == y.station && x.output.id == y.output.id));
        }
    const ItemId picks[] = {itm::PICK_WOOD, itm::PICK_STONE, itm::PICK_COPPER, itm::PICK_BRONZE, itm::PICK_IRON};
    const ItemId axes[] = {itm::AXE_WOOD, itm::AXE_STONE, itm::AXE_COPPER, itm::AXE_BRONZE, itm::AXE_IRON};
    const ItemId shovels[] = {itm::SHOVEL_WOOD, itm::SHOVEL_STONE, itm::SHOVEL_COPPER, itm::SHOVEL_BRONZE,
                              itm::SHOVEL_IRON};
    const ItemId swords[] = {itm::SWORD_WOOD, itm::SWORD_STONE, itm::SWORD_COPPER, itm::SWORD_BRONZE, itm::SWORD_IRON};
    for (int t = 0; t < 5; ++t) {
        CHECK(find(picks[t], Station::Workbench) != nullptr);
        CHECK(find(axes[t], Station::Workbench) != nullptr);
        CHECK(find(shovels[t], Station::Workbench) != nullptr);
        CHECK(find(swords[t], Station::Workbench) != nullptr);
    }
}

// The task's recipe amounts, spot-checked per kind.
TEST(test_recipes_table_amounts) {
    auto needs = [](const Recipe* r, ItemId id) {
        if (!r) return 0;
        for (const ItemStack& s : r->inputs)
            if (s.id == id) return static_cast<int>(s.count);
        return 0;
    };
    const Recipe* planks = find_input(blk::PLANKS, blk::PINE_LOG);
    CHECK(planks && planks->output.count == 4 && planks->station == Station::None);
    const Recipe* sticks = find(itm::STICK, Station::None);
    CHECK(needs(sticks, blk::PLANKS) == 2 && sticks->output.count == 4);
    CHECK(needs(find(blk::WORKBENCH, Station::None), blk::PLANKS) == 4);
    const Recipe* torch = find(blk::TORCH, Station::None);
    CHECK(needs(torch, itm::COAL) == 1 && needs(torch, itm::STICK) == 1 && torch->output.count == 4);
    CHECK(needs(find(blk::KILN, Station::Workbench), blk::STONE) == 8);
    CHECK(needs(find(blk::CRATE, Station::Workbench), blk::PLANKS) == 8);
    const Recipe* iron_pick = find(itm::PICK_IRON, Station::Workbench);
    CHECK(needs(iron_pick, itm::IRON_INGOT) == 3 && needs(iron_pick, itm::STICK) == 2);
    const Recipe* stone_axe = find(itm::AXE_STONE, Station::Workbench);
    CHECK(needs(stone_axe, blk::STONE) == 3 && needs(stone_axe, itm::STICK) == 2);
    const Recipe* copper_shovel = find(itm::SHOVEL_COPPER, Station::Workbench);
    CHECK(needs(copper_shovel, itm::COPPER_INGOT) == 1 && needs(copper_shovel, itm::STICK) == 2);
    const Recipe* bronze_sword = find(itm::SWORD_BRONZE, Station::Workbench);
    CHECK(needs(bronze_sword, itm::BRONZE_INGOT) == 2 && needs(bronze_sword, itm::STICK) == 1);
    const Recipe* silver = find(itm::SWORD_SILVER, Station::Workbench);
    CHECK(needs(silver, itm::SILVER_INGOT) == 2 && needs(silver, itm::QUARTZ_SHARD) == 1 &&
          needs(silver, itm::STICK) == 1);
    const Recipe* lantern = find(blk::LANTERN, Station::Workbench);
    CHECK(needs(lantern, itm::IRON_INGOT) == 1 && needs(lantern, blk::TORCH) == 1 && needs(lantern, blk::GLASS) == 1);
    CHECK(needs(find(itm::HIDE_HELMET, Station::Workbench), itm::HIDE) == 5);
    CHECK(needs(find(itm::HIDE_CHEST, Station::Workbench), itm::HIDE) == 8);
    CHECK(needs(find(itm::IRON_LEGS, Station::Workbench), itm::IRON_INGOT) == 7);
    CHECK(needs(find(itm::IRON_BOOTS, Station::Workbench), itm::IRON_INGOT) == 4);
    // M5: the bedroll (the inputs the user approved on 2026-11-08).
    const Recipe* bedroll = find(blk::BEDROLL, Station::Workbench);
    CHECK(needs(bedroll, itm::WOOL) == 3 && needs(bedroll, blk::PLANKS) == 3 && bedroll->output.count == 1);
    CHECK(bedroll->inputs.size() == 2);
    // Every output of these recipes is 1 except planks, sticks, and torches.
    CHECK(find(itm::PICK_IRON, Station::Workbench)->output.count == 1);
}

// Inputs may come from the hotbar and the last inventory slot; a freed hotbar slot takes output.
TEST(test_craft_from_hotbar_and_inventory_slots) {
    Inventory inv;
    inv.slots[8] = stack(itm::COAL, 1);
    inv.slots[35] = stack(itm::STICK, 1);
    const Recipe* torch = find(blk::TORCH, Station::None);
    CHECK(torch && craft(inv, *torch));
    CHECK(inv.count(blk::TORCH) == 4);
    CHECK(inv.slots[8].empty() || inv.slots[8].id == blk::TORCH);
}
