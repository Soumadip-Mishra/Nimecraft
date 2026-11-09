// Tests for src/inventory.*: adding, removing, and counting stacks.
#include "block.hpp"
#include "inventory.hpp"
#include "items.hpp"
#include "test.hpp"

namespace {

ItemStack stack(ItemId id, int count) {
    return ItemStack{id, static_cast<uint8_t>(count), 0};
}

bool same(const Inventory& a, const Inventory& b) {
    for (std::size_t i = 0; i < a.slots.size(); ++i)
        if (a.slots[i].id != b.slots[i].id || a.slots[i].count != b.slots[i].count ||
            a.slots[i].damage != b.slots[i].damage)
            return false;
    return true;
}

}  // namespace

TEST(test_inventory_stacking) {
    Inventory inv;
    CHECK(inv.add(stack(blk::DIRT, 100)) == 0);
    CHECK(inv.slots[0].id == blk::DIRT);
    CHECK(inv.slots[0].count == 64);
    CHECK(inv.slots[1].id == blk::DIRT);
    CHECK(inv.slots[1].count == 36);
    CHECK(inv.slots[2].empty());
    // Exactly one full stack fills one slot.
    Inventory one;
    CHECK(one.add(stack(blk::STONE, 64)) == 0);
    CHECK(one.slots[0].count == 64);
    CHECK(one.slots[1].empty());
}

TEST(test_inventory_merges_existing_first) {
    Inventory inv;
    inv.slots[20] = stack(blk::DIRT, 60);
    CHECK(inv.add(stack(blk::DIRT, 10)) == 0);
    CHECK(inv.slots[20].count == 64);
    CHECK(inv.slots[0].id == blk::DIRT);
    CHECK(inv.slots[0].count == 6);
}

TEST(test_inventory_tools_dont_stack) {
    Inventory inv;
    CHECK(inv.add(ItemStack{itm::PICK_WOOD, 1, 0}) == 0);
    CHECK(inv.add(ItemStack{itm::PICK_WOOD, 1, 0}) == 0);
    CHECK(inv.slots[0].id == itm::PICK_WOOD && inv.slots[0].count == 1);
    CHECK(inv.slots[1].id == itm::PICK_WOOD && inv.slots[1].count == 1);
    // A damaged tool keeps its damage.
    CHECK(inv.add(ItemStack{itm::PICK_WOOD, 1, 17}) == 0);
    CHECK(inv.slots[2].damage == 17);
    // A stack of 3 tools (never made by the game) still lands as three single tools.
    CHECK(inv.add(ItemStack{itm::AXE_STONE, 3, 0}) == 0);
    for (std::size_t i = 3; i < 6; ++i) CHECK(inv.slots[i].id == itm::AXE_STONE && inv.slots[i].count == 1);
}

TEST(test_inventory_full_leftover) {
    Inventory inv;
    for (auto& s : inv.slots) s = stack(blk::STONE, 64);
    const Inventory before = inv;
    CHECK(inv.add(stack(blk::DIRT, 10)) == 10);
    CHECK(same(inv, before));
    CHECK(inv.add(stack(blk::STONE, 3)) == 3);  // every STONE stack is full
    CHECK(same(inv, before));
}

TEST(test_inventory_partial_fit) {
    Inventory inv;
    for (int i = 0; i < 35; ++i) inv.slots[static_cast<std::size_t>(i)] = stack(blk::STONE, 64);
    inv.slots[35] = stack(blk::DIRT, 60);
    CHECK(inv.add(stack(blk::DIRT, 10)) == 6);
    CHECK(inv.slots[35].count == 64);
}

TEST(test_inventory_add_invalid) {
    Inventory inv;
    const Inventory before = inv;
    CHECK(inv.add(stack(blk::DIRT, 0)) == 0);
    CHECK(same(inv, before));
    CHECK(inv.add(stack(9999, 7)) == 7);
    CHECK(same(inv, before));
    CHECK(inv.add(stack(0, 5)) == 0);  // an AIR stack is empty: nothing to add
    CHECK(same(inv, before));
    CHECK(inv.add(stack(blk::COUNT, 3)) == 3);  // unused block id
    CHECK(same(inv, before));
}

TEST(test_inventory_remove_all_or_nothing) {
    Inventory inv;
    inv.slots[3] = stack(blk::DIRT, 6);
    inv.slots[30] = stack(blk::DIRT, 4);
    const Inventory before = inv;
    CHECK(!inv.remove(blk::DIRT, 11));
    CHECK(same(inv, before));
    CHECK(inv.remove(blk::DIRT, 0));
    CHECK(same(inv, before));
    CHECK(!inv.remove(blk::DIRT, -1));
    CHECK(same(inv, before));
    CHECK(inv.remove(blk::DIRT, 10));
    CHECK(inv.slots[3].empty());
    CHECK(inv.slots[30].empty());
    CHECK(inv.slots[3].id == 0 && inv.slots[30].id == 0);  // emptied slots are cleared
    // A partial remove leaves the rest.
    inv.slots[0] = stack(blk::SAND, 5);
    CHECK(inv.remove(blk::SAND, 2));
    CHECK(inv.slots[0].count == 3);
    CHECK(!inv.remove(blk::GRAVEL, 1));
}

TEST(test_inventory_count_and_held) {
    Inventory inv;
    CHECK(inv.count(blk::DIRT) == 0);
    inv.slots[0] = stack(blk::DIRT, 5);
    inv.slots[9] = stack(blk::DIRT, 64);
    inv.slots[35] = stack(blk::DIRT, 1);
    inv.slots[4] = stack(blk::STONE, 2);
    inv.armor[0] = ItemStack{itm::HIDE_HELMET, 1, 0};
    CHECK(inv.count(blk::DIRT) == 70);
    CHECK(inv.count(blk::STONE) == 2);
    CHECK(inv.count(itm::HIDE_HELMET) == 0);  // armor slots are not counted
    CHECK(inv.count(0) == 0);
    inv.slots[8] = stack(blk::SAND, 9);
    inv.selected = 8;
    CHECK(&inv.held() == &inv.slots[8]);
    inv.selected = 0;
    CHECK(&inv.held() == &inv.slots[0]);
    // An out-of-range selection is clamped to the hotbar.
    inv.selected = 12;
    CHECK(&inv.held() == &inv.slots[8]);
    inv.selected = -3;
    CHECK(&inv.held() == &inv.slots[0]);
}

// Coverage row 3: an empty inventory and adding exactly 36 full stacks.
TEST(test_inventory_fill_exactly) {
    Inventory inv;
    for (int i = 0; i < 36; ++i) CHECK(inv.add(stack(blk::GRAVEL, 64)) == 0);
    CHECK(inv.count(blk::GRAVEL) == 36 * 64);
    CHECK(inv.add(stack(blk::GRAVEL, 1)) == 1);
}
