// Tests for src/ui_logic.*: slot clicks, quick move, slot rules, double tap, palette, layout math.
#include "block.hpp"
#include "items.hpp"
#include "test.hpp"
#include "ui_logic.hpp"

#include <algorithm>
#include <array>
#include <limits>
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
    // Every block but AIR, WATER, GRAVE_CAIRN (only made by dying), and SUNROOT_CROP (planted from
    // seeds), then every other item.
    CHECK(p.size() == static_cast<std::size_t>(blk::COUNT - 4 + (itm::END - itm::STICK)));
    CHECK(std::find(p.begin(), p.end(), blk::SUNROOT_CROP) == p.end());
    CHECK(std::find(p.begin(), p.end(), blk::SAPLING) != p.end());
    CHECK(std::find(p.begin(), p.end(), itm::SUNROOT_SEEDS) != p.end());
    CHECK(std::find(p.begin(), p.end(), blk::AIR) == p.end());
    CHECK(std::find(p.begin(), p.end(), blk::WATER) == p.end());
    CHECK(std::find(p.begin(), p.end(), blk::GRAVE_CAIRN) == p.end());
    CHECK(std::find(p.begin(), p.end(), blk::BEDROLL) != p.end());
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

// M5 Task 4: a 10-icon bar for a 0..20 value, in half steps. An icon is full at value >= 2i + 2 and
// half above 2i, so any health left shows at least half an icon.
TEST(test_icon_fill) {
    for (int i = 0; i < 10; ++i) {
        CHECK(icon_fill(20.f, i) == IconFill::Full);
        CHECK(icon_fill(0.f, i) == IconFill::Empty);
    }
    CHECK(icon_fill(19.5f, 8) == IconFill::Full);
    CHECK(icon_fill(19.5f, 9) == IconFill::Half);
    CHECK(icon_fill(7.f, 2) == IconFill::Full);
    CHECK(icon_fill(7.f, 3) == IconFill::Half);
    CHECK(icon_fill(7.f, 4) == IconFill::Empty);
    CHECK(icon_fill(6.f, 2) == IconFill::Full);
    CHECK(icon_fill(6.f, 3) == IconFill::Empty);
    CHECK(icon_fill(0.3f, 0) == IconFill::Half);
    CHECK(icon_fill(0.3f, 1) == IconFill::Empty);
    CHECK(icon_fill(25.f, 9) == IconFill::Full);
    CHECK(icon_fill(-1.f, 0) == IconFill::Empty);
    CHECK(icon_fill(std::numeric_limits<float>::quiet_NaN(), 0) == IconFill::Empty);
}

// ---- M8 Task 3: TextField and widget math ----

TEST(test_textfield_editing) {
    TextField f;
    for (char32_t c : {U'a', U'c'}) f.insert(c);
    CHECK(f.text == "ac");
    CHECK(f.cursor == 2);
    f.left();
    f.insert(U'b');  // in the middle, at the cursor
    CHECK(f.text == "abc");
    CHECK(f.cursor == 2);
    f.left();
    f.left();
    CHECK(f.cursor == 0);
    f.left();  // at 0: nothing
    CHECK(f.cursor == 0);
    f.backspace();  // at 0: nothing
    CHECK(f.text == "abc");
    f.right();
    f.right();
    f.right();
    CHECK(f.cursor == 3);
    f.right();  // at the end: nothing
    CHECK(f.cursor == 3);
    f.backspace();
    CHECK(f.text == "ab");
    CHECK(f.cursor == 2);
    f.left();
    f.backspace();  // removes 'a', keeps 'b'
    CHECK(f.text == "b");
    CHECK(f.cursor == 0);
    // A cursor past the end (set by hand) is pulled back before editing.
    TextField g;
    g.text = "xy";
    g.cursor = 10;
    g.insert(U'z');
    CHECK(g.text == "xyz");
    CHECK(g.cursor == 3);
    // Empty field: every edit is harmless.
    TextField e;
    e.backspace();
    e.left();
    e.right();
    CHECK(e.text.empty() && e.cursor == 0);
}

TEST(test_textfield_utf8) {
    TextField f;
    f.insert(U'M');
    f.insert(U'é');  // é: 2 bytes
    CHECK(f.text == "M\xC3\xA9");
    CHECK(f.text.size() == 3);
    CHECK(utf8_length(f.text) == 2);
    CHECK(f.cursor == 3);
    f.left();  // over the whole é
    CHECK(f.cursor == 1);
    f.right();
    CHECK(f.cursor == 3);
    f.backspace();  // the whole character
    CHECK(f.text == "M");
    CHECK(f.cursor == 1);
    // 3- and 4-byte characters.
    f.insert(U'€');      // euro sign
    f.insert(U'\U0001F600');  // an emoji
    CHECK(f.text == "M\xE2\x82\xAC\xF0\x9F\x98\x80");
    CHECK(utf8_length(f.text) == 3);
    f.left();
    CHECK(f.cursor == 4);
    f.left();
    CHECK(f.cursor == 1);
    f.right();
    f.right();
    CHECK(f.cursor == 8);
    f.backspace();
    CHECK(f.text == "M\xE2\x82\xAC");
    f.backspace();
    CHECK(f.text == "M");
    // The highest valid code point and a 2-byte edge.
    TextField h;
    h.insert(U'\U0010FFFF');
    h.insert(U'\u0080');
    h.insert(U'߿');
    h.insert(U'ࠀ');
    CHECK(h.text == "\xF4\x8F\xBF\xBF\xC2\x80\xDF\xBF\xE0\xA0\x80");
    CHECK(utf8_length(h.text) == 4);
    CHECK(utf8_length("") == 0);
}

TEST(test_textfield_max_len) {
    TextField f;
    for (int i = 0; i < 32; ++i) f.insert(U'a');
    CHECK(utf8_length(f.text) == 32);
    f.insert(U'b');  // the 33rd is refused
    CHECK(utf8_length(f.text) == 32);
    CHECK(f.text.find('b') == std::string::npos);
    f.backspace();
    f.insert(U'é');  // 32 characters again, 33 bytes: still accepted
    CHECK(utf8_length(f.text) == 32);
    CHECK(f.text.size() == 33);
    f.insert(U'c');
    CHECK(utf8_length(f.text) == 32);
    // A smaller max_len, and max_len 0 refuses everything.
    TextField s;
    s.max_len = 3;
    for (char32_t c : {U'1', U'2', U'3', U'4'}) s.insert(c);
    CHECK(s.text == "123");
    TextField z;
    z.max_len = 0;
    z.insert(U'a');
    CHECK(z.text.empty());
    // Control characters below 0x20 are refused, 0x20 (space) is accepted.
    TextField c;
    for (char32_t ch = 0; ch < 0x20; ++ch) c.insert(ch);
    CHECK(c.text.empty());
    c.insert(U' ');
    CHECK(c.text == " ");
    CHECK(c.cursor == 1);
}

TEST(test_textfield_invalid_codepoints) {
    TextField f;
    f.insert(0x7F);          // DEL
    f.insert(0xD800);        // surrogates
    f.insert(0xDFFF);
    f.insert(0x110000);      // past Unicode
    f.insert(0xFFFFFFFFu);
    CHECK(f.text.empty());
    CHECK(f.cursor == 0);
    f.insert(0xD7FF);  // just below the surrogates
    f.insert(0xE000);  // just above
    CHECK(utf8_length(f.text) == 2);
}

TEST(test_gui_factor) {
    CHECK(gui_factor(1) == 0.75f);
    CHECK(gui_factor(2) == 1.f);
    CHECK(gui_factor(3) == 1.25f);
    CHECK(gui_factor(4) == 1.5f);
    CHECK(gui_factor(0) == 0.75f);
    CHECK(gui_factor(-7) == 0.75f);
    CHECK(gui_factor(5) == 1.5f);
}

TEST(test_slider_value_at) {
    const Rectangle track{100, 0, 200, 20};
    CHECK(slider_value_at(100, track, 0.f, 1.f, 0.f) == 0.f);
    CHECK(slider_value_at(300, track, 0.f, 1.f, 0.f) == 1.f);
    CHECK_NEAR(slider_value_at(150, track, 0.f, 1.f, 0.f), 0.25, 1e-6);
    CHECK(slider_value_at(50, track, 0.f, 1.f, 0.f) == 0.f);   // left of the track
    CHECK(slider_value_at(999, track, 0.f, 1.f, 0.f) == 1.f);  // right of it
    // Integer ranges snap: render distance 4..16 step 1; 37.5% of the way is 8.5 -> 9 (nearest).
    CHECK(slider_value_at(175, track, 4.f, 16.f, 1.f) == 9.f);
    CHECK(slider_value_at(170, track, 4.f, 16.f, 1.f) == 8.f);   // 8.2 -> 8
    CHECK(slider_value_at(100, track, 60.f, 100.f, 5.f) == 60.f);
    CHECK(slider_value_at(212, track, 60.f, 100.f, 5.f) == 80.f);  // 82.4 -> 80
    CHECK(slider_value_at(300, track, 0.1f, 3.f, 0.1f) == 3.f);    // snapping never leaves the range
    // Degenerate input.
    CHECK(slider_value_at(150, Rectangle{100, 0, 0, 20}, 2.f, 5.f, 0.f) == 2.f);
    CHECK(slider_value_at(std::numeric_limits<float>::quiet_NaN(), track, 2.f, 5.f, 0.f) == 2.f);
    // Fraction.
    CHECK(slider_fraction(0.5f, 0.f, 1.f) == 0.5f);
    CHECK(slider_fraction(10.f, 4.f, 16.f) == 0.5f);
    CHECK(slider_fraction(-1.f, 0.f, 1.f) == 0.f);
    CHECK(slider_fraction(2.f, 0.f, 1.f) == 1.f);
    CHECK(slider_fraction(3.f, 5.f, 5.f) == 0.f);
    CHECK(slider_fraction(std::numeric_limits<float>::quiet_NaN(), 0.f, 1.f) == 0.f);
}

TEST(test_list_row_at) {
    const Rectangle area{10, 20, 300, 100};  // 5 rows of 20 px
    CHECK(list_row_at(Vector2{15, 20}, area, 20.f, 0, 10) == 0);
    CHECK(list_row_at(Vector2{15, 39.9f}, area, 20.f, 0, 10) == 0);
    CHECK(list_row_at(Vector2{15, 40}, area, 20.f, 0, 10) == 1);
    CHECK(list_row_at(Vector2{15, 119}, area, 20.f, 0, 10) == 4);
    CHECK(list_row_at(Vector2{15, 45}, area, 20.f, 3, 10) == 4);  // scrolled by 3
    CHECK(list_row_at(Vector2{15, 120}, area, 20.f, 0, 10) == -1);  // below the area
    CHECK(list_row_at(Vector2{15, 19}, area, 20.f, 0, 10) == -1);   // above
    CHECK(list_row_at(Vector2{9, 50}, area, 20.f, 0, 10) == -1);    // left
    CHECK(list_row_at(Vector2{311, 50}, area, 20.f, 0, 10) == -1);  // right
    CHECK(list_row_at(Vector2{15, 70}, area, 20.f, 0, 2) == -1);    // past the last row
    CHECK(list_row_at(Vector2{15, 50}, area, 20.f, 0, 2) == 1);
    CHECK(list_row_at(Vector2{15, 50}, area, 20.f, 0, 0) == -1);    // empty list
    CHECK(list_row_at(Vector2{15, 50}, area, 0.f, 0, 10) == -1);
    CHECK(list_row_at(Vector2{15, 50}, area, 20.f, -4, 10) == 1);   // negative scroll counts as 0
}

// ---- M8 Task 4: the menu state machine ----

TEST(test_menu_transitions_allowed) {
    struct Step {
        Screen from;
        MenuAction a;
        Screen to;
    };
    const Step steps[] = {
        {Screen::Title, MenuAction::Play, Screen::WorldList},
        {Screen::WorldList, MenuAction::Back, Screen::Title},
        {Screen::Title, MenuAction::Play, Screen::WorldList},
        {Screen::WorldList, MenuAction::Create, Screen::CreateWorld},
        {Screen::CreateWorld, MenuAction::Back, Screen::WorldList},
        {Screen::WorldList, MenuAction::Delete, Screen::ConfirmDelete},
        {Screen::ConfirmDelete, MenuAction::Back, Screen::WorldList},
        {Screen::WorldList, MenuAction::Delete, Screen::ConfirmDelete},
        {Screen::ConfirmDelete, MenuAction::Confirm, Screen::WorldList},
        {Screen::WorldList, MenuAction::Create, Screen::CreateWorld},
        {Screen::CreateWorld, MenuAction::Create, Screen::Loading},
        {Screen::Loading, MenuAction::Back, Screen::WorldList},  // the world failed to open
        {Screen::WorldList, MenuAction::Play, Screen::Loading},
        {Screen::Loading, MenuAction::Loaded, Screen::Playing},
        {Screen::Playing, MenuAction::Pause, Screen::Paused},
        {Screen::Paused, MenuAction::Resume, Screen::Playing},
        {Screen::Playing, MenuAction::Pause, Screen::Paused},
        {Screen::Paused, MenuAction::Back, Screen::Playing},  // Esc again
        {Screen::Playing, MenuAction::Pause, Screen::Paused},
        {Screen::Paused, MenuAction::OpenSettings, Screen::Settings},
        {Screen::Settings, MenuAction::Back, Screen::Paused},  // back where it was opened
        {Screen::Paused, MenuAction::Resume, Screen::Playing},
        {Screen::Playing, MenuAction::Died, Screen::Death},
        {Screen::Death, MenuAction::Respawn, Screen::Playing},
        {Screen::Playing, MenuAction::Died, Screen::Death},
        {Screen::Death, MenuAction::SaveAndQuit, Screen::Title},
        {Screen::Title, MenuAction::OpenSettings, Screen::Settings},
        {Screen::Settings, MenuAction::Back, Screen::Title},
        {Screen::Title, MenuAction::Play, Screen::WorldList},
        {Screen::WorldList, MenuAction::Play, Screen::Loading},
        {Screen::Loading, MenuAction::Loaded, Screen::Playing},
        {Screen::Playing, MenuAction::Pause, Screen::Paused},
        {Screen::Paused, MenuAction::SaveAndQuit, Screen::Title},
    };
    MenuState m;
    CHECK(m.screen == Screen::Title);
    CHECK(!m.quit);
    for (const Step& s : steps) {
        CHECK(m.screen == s.from);
        CHECK(m.apply(s.a));
        CHECK(m.screen == s.to);
    }
    CHECK(m.apply(MenuAction::Quit));  // Title: Quit ends the program, the screen stays
    CHECK(m.quit);
    CHECK(m.screen == Screen::Title);
    // A world that was saved on the death screen loads straight into Death.
    MenuState d;
    d.screen = Screen::Loading;
    CHECK(d.apply(MenuAction::Loaded));
    CHECK(d.apply(MenuAction::Died));
    CHECK(d.screen == Screen::Death);
}

TEST(test_menu_transitions_refused) {
    // Exactly these (screen, action) pairs are offered; every other pair changes nothing.
    const int screens = 9, actions = 13;
    int allowed = 0;
    for (int s = 0; s < screens; ++s)
        for (int a = 0; a < actions; ++a) {
            MenuState m;
            m.screen = static_cast<Screen>(s);
            m.settings_return = Screen::Paused;
            const bool ok = m.apply(static_cast<MenuAction>(a));
            if (ok) {
                ++allowed;
            } else {
                CHECK(m.screen == static_cast<Screen>(s));
                CHECK(m.settings_return == Screen::Paused);
                CHECK(!m.quit);
            }
        }
    // Title 3, WorldList 4, CreateWorld 2, ConfirmDelete 2, Loading 2, Playing 2, Paused 4,
    // Settings 1, Death 2.
    CHECK(allowed == 22);
    // Some that must not happen.
    MenuState p;
    p.screen = Screen::Playing;
    CHECK(!p.apply(MenuAction::Quit));        // no quitting from inside a world without saving
    CHECK(!p.apply(MenuAction::Respawn));     // alive
    CHECK(!p.apply(MenuAction::SaveAndQuit)); // only from the pause or death screen
    MenuState t;
    CHECK(!t.apply(MenuAction::Loaded));      // nothing is loading
    CHECK(!t.apply(MenuAction::Back));        // the title has no back
    MenuState dl;
    dl.screen = Screen::Death;
    CHECK(!dl.apply(MenuAction::Pause));
    CHECK(!dl.apply(MenuAction::OpenSettings));
    // Out-of-range actions are refused.
    MenuState o;
    CHECK(!o.apply(static_cast<MenuAction>(200)));
    CHECK(o.screen == Screen::Title);
}
