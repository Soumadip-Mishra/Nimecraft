// Tests for src/ui_logic.*: slot clicks, quick move, slot rules, double tap, palette, layout math.
#include "block.hpp"
#include "items.hpp"
#include "test.hpp"
#include "ui_logic.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace {

ItemStack st(ItemId id, int count, int damage = 0) {
    return ItemStack{id, static_cast<uint8_t>(count), static_cast<uint16_t>(damage)};
}

bool is(const ItemStack& s, ItemId id, int count) {
    return !s.empty() && s.id == id && s.count == count;
}

}  // namespace

TEST(test_click_pick_put_swap) {
    ItemStack cursor{}, slot = st(blk::DIRT, 10);
    apply_click(cursor, slot, Click::Left);
    CHECK(is(cursor, blk::DIRT, 10));
    CHECK(slot.empty());
    apply_click(cursor, slot, Click::Left);
    CHECK(cursor.empty());
    CHECK(is(slot, blk::DIRT, 10));
    cursor = st(blk::STONE, 5);
    apply_click(cursor, slot, Click::Left);
    CHECK(is(cursor, blk::DIRT, 10));
    CHECK(is(slot, blk::STONE, 5));
}

TEST(test_click_merge_overflow) {
    ItemStack cursor = st(blk::DIRT, 10), slot = st(blk::DIRT, 60);
    apply_click(cursor, slot, Click::Left);
    CHECK(is(slot, blk::DIRT, 64));
    CHECK(is(cursor, blk::DIRT, 6));
    // Onto a full stack of the same item: nothing moves.
    apply_click(cursor, slot, Click::Left);
    CHECK(is(slot, blk::DIRT, 64));
    CHECK(is(cursor, blk::DIRT, 6));
    // Everything fits: the cursor empties.
    ItemStack c2 = st(blk::SAND, 4), s2 = st(blk::SAND, 60);
    apply_click(c2, s2, Click::Left);
    CHECK(is(s2, blk::SAND, 64));
    CHECK(c2.empty());
}

TEST(test_click_right_half_and_one) {
    ItemStack cursor{}, slot = st(blk::DIRT, 11);
    apply_click(cursor, slot, Click::Right);
    CHECK(is(cursor, blk::DIRT, 6));
    CHECK(is(slot, blk::DIRT, 5));
    ItemStack empty{};
    apply_click(cursor, empty, Click::Right);
    CHECK(is(empty, blk::DIRT, 1));
    CHECK(is(cursor, blk::DIRT, 5));
    ItemStack other = st(blk::STONE, 3);
    apply_click(cursor, other, Click::Right);
    CHECK(is(cursor, blk::DIRT, 5));
    CHECK(is(other, blk::STONE, 3));
    ItemStack c1{}, one = st(blk::DIRT, 1);
    apply_click(c1, one, Click::Right);
    CHECK(is(c1, blk::DIRT, 1));
    CHECK(one.empty());
    // Right onto the same item adds one; onto a full stack nothing; the last one empties the cursor.
    ItemStack c3 = st(blk::DIRT, 1), same = st(blk::DIRT, 7), full = st(blk::DIRT, 64);
    ItemStack c4 = st(blk::DIRT, 2);
    apply_click(c4, full, Click::Right);
    CHECK(is(full, blk::DIRT, 64) && is(c4, blk::DIRT, 2));
    apply_click(c3, same, Click::Right);
    CHECK(is(same, blk::DIRT, 8));
    CHECK(c3.empty());
    // ShiftLeft is the caller's quick move: apply_click changes nothing.
    ItemStack c5{}, s5 = st(blk::DIRT, 3);
    apply_click(c5, s5, Click::ShiftLeft);
    CHECK(c5.empty() && is(s5, blk::DIRT, 3));
}

TEST(test_click_tools_swap) {
    ItemStack cursor = st(itm::PICK_WOOD, 1, 5), slot = st(itm::PICK_WOOD, 1, 9);
    apply_click(cursor, slot, Click::Left);
    CHECK(cursor.damage == 9 && cursor.count == 1);
    CHECK(slot.damage == 5 && slot.count == 1);
    // Right with a tool onto another tool swaps nothing and merges nothing.
    apply_click(cursor, slot, Click::Right);
    CHECK(cursor.damage == 9 && slot.damage == 5);
    // Same id and damage but max_stack 1: still a swap, never 2 in a slot.
    ItemStack a = st(itm::AXE_IRON, 1), b = st(itm::AXE_IRON, 1);
    apply_click(a, b, Click::Left);
    CHECK(a.count == 1 && b.count == 1);
}

TEST(test_click_empty_both) {
    ItemStack cursor{}, slot{};
    for (Click c : {Click::Left, Click::Right, Click::ShiftLeft}) {
        apply_click(cursor, slot, c);
        CHECK(cursor.empty() && slot.empty());
    }
}

TEST(test_quick_move) {
    std::array<ItemStack, 4> to = {st(blk::DIRT, 60), ItemStack{}, st(blk::STONE, 5), st(blk::DIRT, 63)};
    ItemStack from = st(blk::DIRT, 10);
    CHECK(quick_move(from, to));
    CHECK(from.empty());
    CHECK(is(to[0], blk::DIRT, 64));  // partial stacks first, in order
    CHECK(is(to[3], blk::DIRT, 64));
    CHECK(is(to[1], blk::DIRT, 5));  // then the empty slot
    CHECK(is(to[2], blk::STONE, 5));

    std::array<ItemStack, 2> full = {st(blk::STONE, 64), st(blk::DIRT, 64)};
    ItemStack f = st(blk::DIRT, 3);
    CHECK(!quick_move(f, full));
    CHECK(is(f, blk::DIRT, 3));
    CHECK(is(full[1], blk::DIRT, 64));

    std::array<ItemStack, 1> partial = {st(blk::DIRT, 62)};
    ItemStack p = st(blk::DIRT, 5);
    CHECK(quick_move(p, partial));
    CHECK(is(partial[0], blk::DIRT, 64));
    CHECK(is(p, blk::DIRT, 3));

    ItemStack nothing{};
    CHECK(!quick_move(nothing, partial));
    CHECK(!quick_move(p, std::span<ItemStack>{}));
    // Tools only go into empty slots.
    std::array<ItemStack, 2> tools = {st(itm::PICK_WOOD, 1), ItemStack{}};
    ItemStack pick = st(itm::PICK_WOOD, 1, 3);
    CHECK(quick_move(pick, tools));
    CHECK(pick.empty());
    CHECK(tools[1].damage == 3);
}

TEST(test_slot_rules) {
    CHECK(slot_accepts(SlotKind::Any, st(blk::DIRT, 1)));
    CHECK(slot_accepts(SlotKind::Head, st(itm::IRON_HELMET, 1)));
    CHECK(!slot_accepts(SlotKind::Head, st(itm::IRON_CHEST, 1)));
    CHECK(slot_accepts(SlotKind::Chest, st(itm::HIDE_CHEST, 1)));
    CHECK(slot_accepts(SlotKind::Legs, st(itm::HIDE_LEGS, 1)));
    CHECK(slot_accepts(SlotKind::Feet, st(itm::IRON_BOOTS, 1)));
    CHECK(!slot_accepts(SlotKind::Feet, st(blk::DIRT, 1)));
    CHECK(!slot_accepts(SlotKind::TakeOnly, st(blk::DIRT, 1)));
    for (SlotKind k : {SlotKind::Any, SlotKind::Head, SlotKind::TakeOnly}) CHECK(slot_accepts(k, ItemStack{}));

    // An armor slot refuses the wrong item, takes the right one, and gives it back.
    ItemStack cursor = st(blk::DIRT, 5), head{};
    click_slot(cursor, head, SlotKind::Head, Click::Left);
    CHECK(head.empty() && is(cursor, blk::DIRT, 5));
    cursor = st(itm::HIDE_HELMET, 1);
    click_slot(cursor, head, SlotKind::Head, Click::Left);
    CHECK(is(head, itm::HIDE_HELMET, 1) && cursor.empty());
    click_slot(cursor, head, SlotKind::Head, Click::Left);
    CHECK(is(cursor, itm::HIDE_HELMET, 1) && head.empty());
    // Swapping a helmet with a chestplate in the head slot is refused.
    head = st(itm::IRON_HELMET, 1);
    cursor = st(itm::IRON_CHEST, 1);
    click_slot(cursor, head, SlotKind::Head, Click::Left);
    CHECK(is(head, itm::IRON_HELMET, 1) && is(cursor, itm::IRON_CHEST, 1));

    // A take-only output: takes all into an empty cursor, merges into the same item, else nothing.
    ItemStack out = st(itm::COPPER_INGOT, 5), c{};
    click_slot(c, out, SlotKind::TakeOnly, Click::Right);
    CHECK(is(c, itm::COPPER_INGOT, 5) && out.empty());
    out = st(itm::COPPER_INGOT, 3);
    click_slot(c, out, SlotKind::TakeOnly, Click::Left);
    CHECK(is(c, itm::COPPER_INGOT, 8) && out.empty());
    out = st(itm::COPPER_INGOT, 60);
    click_slot(c, out, SlotKind::TakeOnly, Click::Left);  // 68 would not fit the cursor
    CHECK(is(c, itm::COPPER_INGOT, 8) && is(out, itm::COPPER_INGOT, 60));
    ItemStack other = st(blk::DIRT, 1);
    click_slot(other, out, SlotKind::TakeOnly, Click::Left);
    CHECK(is(other, blk::DIRT, 1) && is(out, itm::COPPER_INGOT, 60));
    // Any behaves like apply_click.
    ItemStack a{}, b = st(blk::SAND, 2);
    click_slot(a, b, SlotKind::Any, Click::Left);
    CHECK(is(a, blk::SAND, 2) && b.empty());
}

TEST(test_double_tap) {
    DoubleTap t;
    CHECK(!t.press(10.0));
    CHECK(t.press(10.3));   // exactly 0.3 s later
    CHECK(!t.press(10.4));  // a third press starts over
    CHECK(!t.press(11.0));
    CHECK(!t.press(11.31)); // 0.31 s is too slow
    CHECK(t.press(11.5));
}

TEST(test_creative_palette) {
    const std::vector<ItemId> p = creative_palette();
    CHECK(p.size() == static_cast<std::size_t>(blk::COUNT - 2 + (itm::END - itm::STICK)));
    CHECK(std::find(p.begin(), p.end(), blk::AIR) == p.end());
    CHECK(std::find(p.begin(), p.end(), blk::WATER) == p.end());
    CHECK(std::find(p.begin(), p.end(), blk::GLASS) != p.end());
    CHECK(std::find(p.begin(), p.end(), itm::IRON_BOOTS) != p.end());
    for (ItemId id : p) CHECK(item_def(id).max_stack > 0);
}

TEST(test_grid_slot_at) {
    const Vector2 origin{100, 50};
    // 9 columns of 40 px slots with 4 px gaps: slot 0 spans x 100..140, slot 1 starts at 144.
    CHECK(grid_slot_at(Vector2{100, 50}, origin, 9, 4, 40, 4) == 0);
    CHECK(grid_slot_at(Vector2{139.9f, 89.9f}, origin, 9, 4, 40, 4) == 0);
    CHECK(grid_slot_at(Vector2{142, 60}, origin, 9, 4, 40, 4) == -1);  // in the gap
    CHECK(grid_slot_at(Vector2{140, 60}, origin, 9, 4, 40, 4) == -1);  // exactly at the slot's right edge
    CHECK(grid_slot_at(Vector2{120, 90}, origin, 9, 4, 40, 4) == -1);  // exactly at its bottom edge
    CHECK(grid_slot_at(Vector2{144, 60}, origin, 9, 4, 40, 4) == 1);
    CHECK(grid_slot_at(Vector2{100, 94}, origin, 9, 4, 40, 4) == 9);  // second row
    CHECK(grid_slot_at(Vector2{100 + 8 * 44 + 1, 50 + 3 * 44 + 1}, origin, 9, 4, 40, 4) == 35);
    CHECK(grid_slot_at(Vector2{99, 60}, origin, 9, 4, 40, 4) == -1);
    CHECK(grid_slot_at(Vector2{100 + 9 * 44, 60}, origin, 9, 4, 40, 4) == -1);
    CHECK(grid_slot_at(Vector2{100, 50 + 4 * 44}, origin, 9, 4, 40, 4) == -1);
    const Rectangle r = grid_slot_rect(origin, 10, 9, 40, 4);
    CHECK(r.x == 144.f && r.y == 94.f && r.width == 40.f && r.height == 40.f);
}

TEST(test_clamp_scroll) {
    CHECK(clamp_scroll(0, 20, 8) == 0);
    CHECK(clamp_scroll(12, 20, 8) == 12);
    CHECK(clamp_scroll(13, 20, 8) == 12);
    CHECK(clamp_scroll(-4, 20, 8) == 0);
    CHECK(clamp_scroll(5, 6, 8) == 0);  // everything fits
    CHECK(clamp_scroll(3, 0, 8) == 0);
}
