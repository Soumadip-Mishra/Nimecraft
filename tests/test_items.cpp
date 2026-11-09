// Tests for src/items.*: item ids and the ItemDef table.
#include "block.hpp"
#include "items.hpp"
#include "test.hpp"

#include <string_view>
#include <vector>

namespace {

bool is_tool(ItemId id) {
    return id >= itm::PICK_WOOD && id <= itm::SWORD_SILVER;
}

bool is_armor(ItemId id) {
    return id >= itm::HIDE_HELMET && id <= itm::IRON_BOOTS;
}

}  // namespace

TEST(test_item_ids_pinned) {
    CHECK(itm::STICK == 256);
    CHECK(itm::COAL == 257);
    CHECK(itm::RAW_MEAT == 264);
    // The rest of the list follows the task file's order without gaps.
    CHECK(itm::GLOOM_DUST == 272);
    CHECK(itm::PICK_WOOD == 273);
    CHECK(itm::PICK_IRON == 277);
    CHECK(itm::AXE_WOOD == 278);
    CHECK(itm::SHOVEL_WOOD == 283);
    CHECK(itm::SWORD_WOOD == 288);
    CHECK(itm::SWORD_IRON == 292);
    CHECK(itm::SWORD_SILVER == 293);
    CHECK(itm::HIDE_HELMET == 294);
    CHECK(itm::IRON_BOOTS == 301);
    CHECK(itm::SUNROOT_SEEDS == 302);  // M7
    CHECK(itm::SUNROOT == 303);
    CHECK(itm::END == 304);
    // Block items share the block's id (M4 README block ids).
    CHECK(blk::WORKBENCH == 29);
    CHECK(blk::KILN == 30);
    CHECK(blk::CRATE == 31);
    CHECK(blk::GLASS == 32);
    // M5 README.
    CHECK(blk::BEDROLL == 33);
    CHECK(blk::GRAVE_CAIRN == 34);
    CHECK(blk::COUNT == 38);
}

TEST(test_item_defs_table) {
    const ItemDef& pick = item_def(itm::PICK_IRON);
    CHECK(pick.tool == ToolKind::Pick);
    CHECK(pick.tier == 5);
    CHECK(pick.durability == 500);
    const ItemDef& silver = item_def(itm::SWORD_SILVER);
    CHECK(silver.tool == ToolKind::Sword);
    CHECK(silver.tier == 3);
    CHECK(silver.durability == 250);
    CHECK(silver.attack == 5.5f);
    CHECK(item_def(itm::AXE_BRONZE).attack == 4.f);
    CHECK(item_def(itm::AXE_BRONZE).tool == ToolKind::Axe);
    CHECK(item_def(itm::COOKED_MEAT).food == 8);
    CHECK(item_def(itm::COOKED_MEAT).cooked);
    CHECK(item_def(itm::RAW_MEAT).food == 3);
    CHECK(!item_def(itm::RAW_MEAT).cooked);
    CHECK(item_def(itm::SMALL_MEAT).food == 2);
    CHECK(item_def(itm::COOKED_SMALL_MEAT).food == 5);
    CHECK(item_def(itm::COOKED_SMALL_MEAT).cooked);
    CHECK(item_def(itm::SUNROOT).food == 4);  // M7: eaten raw
    CHECK(!item_def(itm::SUNROOT).cooked);
    CHECK(item_def(itm::SUNROOT_SEEDS).food == 0);
    CHECK(item_def(itm::IRON_CHEST).armor == ArmorSlot::Chest);
    CHECK(item_def(itm::IRON_CHEST).armor_points == 6);
    CHECK(item_def(itm::HIDE_BOOTS).armor == ArmorSlot::Feet);
    CHECK(item_def(itm::HIDE_BOOTS).armor_points == 1);

    for (ItemId id = 1; id < itm::END; ++id) {
        if (id >= blk::COUNT && id < itm::STICK) continue;  // unused ids: AIR items
        const ItemDef& d = item_def(id);
        CHECK(d.max_stack == ((is_tool(id) || is_armor(id)) ? 1 : 64));
        CHECK(!d.name.empty());
    }
    // Unique names over every block id below blk::COUNT and every item id below itm::END.
    std::vector<std::string_view> names;
    for (ItemId id = 0; id < blk::COUNT; ++id) names.push_back(item_def(id).name);
    for (ItemId id = itm::STICK; id < itm::END; ++id) names.push_back(item_def(id).name);
    for (std::size_t a = 0; a < names.size(); ++a)
        for (std::size_t b = a + 1; b < names.size(); ++b) CHECK(names[a] != names[b]);

    CHECK(item_def(9999).max_stack == 0);
    CHECK(item_def(itm::END).max_stack == 0);
    CHECK(item_def(0).max_stack == 0);  // AIR is no item
    for (ItemId id = blk::COUNT; id < itm::STICK; ++id) CHECK(&item_def(id) == &item_def(0));
    CHECK(&item_def(9999) == &item_def(0));
    // A block item carries its block's name.
    CHECK(item_def(blk::STONE).name == block_def(blk::STONE).name);
    CHECK(item_def(blk::KILN).name == block_def(blk::KILN).name);
}

// Tiers 1..5 wood..iron with the task's durability; swords' attack, other tools sword - 2.
TEST(test_item_tool_tiers) {
    const ItemId picks[] = {itm::PICK_WOOD, itm::PICK_STONE, itm::PICK_COPPER, itm::PICK_BRONZE, itm::PICK_IRON};
    const ItemId axes[] = {itm::AXE_WOOD, itm::AXE_STONE, itm::AXE_COPPER, itm::AXE_BRONZE, itm::AXE_IRON};
    const ItemId shovels[] = {itm::SHOVEL_WOOD, itm::SHOVEL_STONE, itm::SHOVEL_COPPER, itm::SHOVEL_BRONZE,
                              itm::SHOVEL_IRON};
    const ItemId swords[] = {itm::SWORD_WOOD, itm::SWORD_STONE, itm::SWORD_COPPER, itm::SWORD_BRONZE, itm::SWORD_IRON};
    const int durability[] = {60, 130, 200, 350, 500};
    const float sword_attack[] = {4.f, 5.f, 5.5f, 6.f, 7.f};
    for (int t = 0; t < 5; ++t) {
        const ItemDef* defs[] = {&item_def(picks[t]), &item_def(axes[t]), &item_def(shovels[t]), &item_def(swords[t])};
        const ToolKind kinds[] = {ToolKind::Pick, ToolKind::Axe, ToolKind::Shovel, ToolKind::Sword};
        for (int k = 0; k < 4; ++k) {
            CHECK(defs[k]->tool == kinds[k]);
            CHECK(defs[k]->tier == t + 1);
            CHECK(defs[k]->durability == durability[t]);
            CHECK(defs[k]->attack == (k == 3 ? sword_attack[t] : sword_attack[t] - 2.f));
            CHECK(defs[k]->food == 0);
        }
    }
    // Hand and non-tools: tier 0, attack 1, no durability.
    CHECK(item_def(blk::DIRT).tool == ToolKind::None);
    CHECK(item_def(blk::DIRT).tier == 0);
    CHECK(item_def(blk::DIRT).attack == 1.f);
    CHECK(item_def(itm::STICK).durability == 0);
    CHECK(item_def(itm::COAL).attack == 1.f);
}

TEST(test_item_armor_table) {
    const ItemId hide[] = {itm::HIDE_HELMET, itm::HIDE_CHEST, itm::HIDE_LEGS, itm::HIDE_BOOTS};
    const ItemId iron[] = {itm::IRON_HELMET, itm::IRON_CHEST, itm::IRON_LEGS, itm::IRON_BOOTS};
    const ArmorSlot slots[] = {ArmorSlot::Head, ArmorSlot::Chest, ArmorSlot::Legs, ArmorSlot::Feet};
    const int hide_pts[] = {1, 3, 2, 1}, iron_pts[] = {2, 6, 5, 2};
    for (int i = 0; i < 4; ++i) {
        CHECK(item_def(hide[i]).armor == slots[i]);
        CHECK(item_def(hide[i]).armor_points == hide_pts[i]);
        CHECK(item_def(iron[i]).armor == slots[i]);
        CHECK(item_def(iron[i]).armor_points == iron_pts[i]);
        CHECK(item_def(hide[i]).tool == ToolKind::None);
    }
    CHECK(item_def(itm::STICK).armor == ArmorSlot::None);
    CHECK(item_def(blk::STONE).armor == ArmorSlot::None);
}

// Non-block items have distinct item-atlas cells inside the 8x8 grid.
TEST(test_item_icons_distinct) {
    for (ItemId a = itm::STICK; a < itm::END; ++a) {
        CHECK(item_def(a).icon < 64);
        for (ItemId b = static_cast<ItemId>(a + 1); b < itm::END; ++b) CHECK(item_def(a).icon != item_def(b).icon);
    }
}

TEST(test_item_stack_empty) {
    CHECK(ItemStack{}.empty());
    CHECK((ItemStack{blk::DIRT, 0, 0}).empty());
    CHECK((ItemStack{0, 5, 0}).empty());
    CHECK(!(ItemStack{blk::DIRT, 1, 0}).empty());
}
