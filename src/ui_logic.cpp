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

// ---- Menu widgets (M8 Task 3) ----

namespace {

bool continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

}  // namespace

std::size_t utf8_length(std::string_view s) {
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return !continuation(c); }));
}

void TextField::insert(char32_t c) {
    if (c < 0x20 || c == 0x7F || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) return;
    if (utf8_length(text) >= max_len) return;
    cursor = std::min(cursor, text.size());
    char buf[4];
    std::size_t n = 0;
    const auto byte = [](char32_t v) { return static_cast<char>(static_cast<unsigned char>(v)); };
    if (c < 0x80) {
        buf[n++] = byte(c);
    } else if (c < 0x800) {
        buf[n++] = byte(0xC0 | (c >> 6));
        buf[n++] = byte(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        buf[n++] = byte(0xE0 | (c >> 12));
        buf[n++] = byte(0x80 | ((c >> 6) & 0x3F));
        buf[n++] = byte(0x80 | (c & 0x3F));
    } else {
        buf[n++] = byte(0xF0 | (c >> 18));
        buf[n++] = byte(0x80 | ((c >> 12) & 0x3F));
        buf[n++] = byte(0x80 | ((c >> 6) & 0x3F));
        buf[n++] = byte(0x80 | (c & 0x3F));
    }
    text.insert(cursor, buf, n);
    cursor += n;
}

void TextField::left() {
    cursor = std::min(cursor, text.size());
    if (cursor == 0) return;
    do --cursor;
    while (cursor > 0 && continuation(text[cursor]));
}

void TextField::right() {
    if (cursor >= text.size()) {
        cursor = text.size();
        return;
    }
    do ++cursor;
    while (cursor < text.size() && continuation(text[cursor]));
}

void TextField::backspace() {
    cursor = std::min(cursor, text.size());
    const std::size_t end = cursor;
    left();
    text.erase(cursor, end - cursor);
}

float gui_factor(int gui_scale) {
    return static_cast<float>(std::clamp(gui_scale, 1, 4) + 2) / 4.f;
}

float slider_value_at(float x, Rectangle track, float lo, float hi, float step) {
    if (!(track.width > 0.f) || !std::isfinite(x) || !(hi > lo)) return lo;
    const float t = std::clamp((x - track.x) / track.width, 0.f, 1.f);
    float v = lo + (hi - lo) * t;
    if (step > 0.f) v = lo + std::round((v - lo) / step) * step;
    return std::clamp(v, lo, hi);
}

float slider_fraction(float v, float lo, float hi) {
    if (!(hi > lo) || !std::isfinite(v)) return 0.f;
    return std::clamp((v - lo) / (hi - lo), 0.f, 1.f);
}

int list_row_at(Vector2 p, Rectangle area, float row_h, int scroll, int count) {
    if (!(row_h > 0.f) || !(p.x >= area.x) || p.x >= area.x + area.width || !(p.y >= area.y) ||
        p.y >= area.y + area.height)
        return -1;
    const int row = std::max(0, scroll) + static_cast<int>(std::floor((p.y - area.y) / row_h));
    return row < count ? row : -1;
}

bool MenuState::apply(MenuAction a) {
    using A = MenuAction;
    using S = Screen;
    const auto go = [this](S to) {
        screen = to;
        return true;
    };
    switch (screen) {
    case S::Title:
        if (a == A::Play) return go(S::WorldList);
        if (a == A::OpenSettings) {
            settings_return = S::Title;
            return go(S::Settings);
        }
        if (a == A::Quit) return quit = true;
        return false;
    case S::WorldList:
        if (a == A::Play) return go(S::Loading);
        if (a == A::Create) return go(S::CreateWorld);
        if (a == A::Delete) return go(S::ConfirmDelete);
        if (a == A::Back) return go(S::Title);
        return false;
    case S::CreateWorld:
        if (a == A::Create) return go(S::Loading);
        if (a == A::Back) return go(S::WorldList);
        return false;
    case S::ConfirmDelete:
        if (a == A::Confirm || a == A::Back) return go(S::WorldList);
        return false;
    case S::Loading:
        if (a == A::Loaded) return go(S::Playing);
        if (a == A::Back) return go(S::WorldList);
        return false;
    case S::Playing:
        if (a == A::Pause) return go(S::Paused);
        if (a == A::Died) return go(S::Death);
        return false;
    case S::Paused:
        if (a == A::Resume || a == A::Back) return go(S::Playing);
        if (a == A::OpenSettings) {
            settings_return = S::Paused;
            return go(S::Settings);
        }
        if (a == A::SaveAndQuit) return go(S::Title);
        return false;
    case S::Settings:
        if (a == A::Back) return go(settings_return);
        return false;
    case S::Death:
        if (a == A::Respawn) return go(S::Playing);
        if (a == A::SaveAndQuit) return go(S::Title);
        return false;
    }
    return false;
}
