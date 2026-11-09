#include "ui_logic.hpp"

#include "block.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr double DOUBLE_TAP_S = 0.3, TIME_EPS = 1e-9;  // EPS: 10.3 - 10.0 is 0.3000000000000007

bool stacks_with(const ItemStack& a, const ItemStack& b) {
    return !a.empty() && !b.empty() && a.id == b.id && a.damage == b.damage && item_def(a.id).max_stack > 1;
}

// Moves up to n from `from` onto `to` (same item or empty), at most to max_stack; returns moved.
int move_some(ItemStack& from, ItemStack& to, int n) {
    const int max = item_def(from.id).max_stack;
    const int have = to.empty() ? 0 : to.count;
    const int moved = std::min({n, static_cast<int>(from.count), max - have});
    if (moved <= 0) return 0;
    to = ItemStack{from.id, static_cast<uint8_t>(have + moved), from.damage};
    from.count = static_cast<uint8_t>(from.count - moved);
    if (from.count == 0) from = ItemStack{};
    return moved;
}

}  // namespace

void apply_click(ItemStack& cursor, ItemStack& slot, Click c) {
    if (c == Click::ShiftLeft) return;
    if (cursor.empty() && slot.empty()) return;
    if (c == Click::Left) {
        if (stacks_with(cursor, slot)) {
            move_some(cursor, slot, cursor.count);
            return;
        }
        std::swap(cursor, slot);
        if (cursor.empty()) cursor = ItemStack{};
        if (slot.empty()) slot = ItemStack{};
        return;
    }
    // Right.
    if (cursor.empty()) {
        const int half = (slot.count + 1) / 2;
        move_some(slot, cursor, half);
        return;
    }
    if (slot.empty() || stacks_with(cursor, slot)) move_some(cursor, slot, 1);
}

bool quick_move(ItemStack& from, std::span<ItemStack> to) {
    if (from.empty()) return false;
    const int before = from.count;
    for (ItemStack& t : to)
        if (!from.empty() && stacks_with(from, t)) move_some(from, t, from.count);
    for (ItemStack& t : to)
        if (!from.empty() && t.empty()) move_some(from, t, from.count);
    return from.empty() || from.count != before;
}

bool slot_accepts(SlotKind k, const ItemStack& s) {
    if (s.empty()) return true;
    switch (k) {
    case SlotKind::Any: return true;
    case SlotKind::Head: return item_def(s.id).armor == ArmorSlot::Head;
    case SlotKind::Chest: return item_def(s.id).armor == ArmorSlot::Chest;
    case SlotKind::Legs: return item_def(s.id).armor == ArmorSlot::Legs;
    case SlotKind::Feet: return item_def(s.id).armor == ArmorSlot::Feet;
    default: return false;  // TakeOnly
    }
}

void click_slot(ItemStack& cursor, ItemStack& slot, SlotKind kind, Click c) {
    if (kind == SlotKind::TakeOnly) {
        if (slot.empty() || c == Click::ShiftLeft) return;
        if (cursor.empty()) {
            std::swap(cursor, slot);
            slot = ItemStack{};
        } else if (stacks_with(cursor, slot) && cursor.count + slot.count <= item_def(cursor.id).max_stack) {
            move_some(slot, cursor, slot.count);
        }
        return;
    }
    if (!slot_accepts(kind, cursor)) return;
    apply_click(cursor, slot, c);
}

bool DoubleTap::press(double now) {
    if (now - last <= DOUBLE_TAP_S + TIME_EPS) {
        last = -1.0e9;
        return true;
    }
    last = now;
    return false;
}

IconFill icon_fill(float value, int i) {
    const float lo = 2.f * static_cast<float>(i);
    if (value >= lo + 2.f) return IconFill::Full;
    if (value > lo) return IconFill::Half;
    return IconFill::Empty;
}

std::vector<ItemId> creative_palette() {
    std::vector<ItemId> out;
    for (BlockId b = 1; b < blk::COUNT; ++b)
        if (b != blk::WATER && b != blk::GRAVE_CAIRN && b != blk::SUNROOT_CROP) out.push_back(b);
    for (ItemId id = itm::STICK; id < itm::END; ++id) out.push_back(id);
    return out;
}

int grid_slot_at(Vector2 p, Vector2 origin, int cols, int rows, float size, float gap) {
    const float step = size + gap;
    const float fx = (p.x - origin.x) / step, fy = (p.y - origin.y) / step;
    if (!(fx >= 0.f) || !(fy >= 0.f)) return -1;
    const int col = static_cast<int>(std::floor(fx)), row = static_cast<int>(std::floor(fy));
    if (col >= cols || row >= rows) return -1;
    const float inx = p.x - origin.x - static_cast<float>(col) * step;
    const float iny = p.y - origin.y - static_cast<float>(row) * step;
    if (inx >= size || iny >= size) return -1;  // in the gap after the slot
    return row * cols + col;
}

Rectangle grid_slot_rect(Vector2 origin, int index, int cols, float size, float gap) {
    const float step = size + gap;
    return Rectangle{origin.x + static_cast<float>(index % cols) * step,
                     origin.y + static_cast<float>(index / cols) * step, size, size};
}

int clamp_scroll(int scroll, int rows, int visible) {
    return std::clamp(scroll, 0, std::max(0, rows - visible));
}
