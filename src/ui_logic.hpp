// Window-free inventory UI rules: slot clicks, quick move, slot kinds, double tap, the Creative
// palette, and slot grid geometry. Logic only: raylib for Vector2 and Rectangle.
#pragma once

#include "items.hpp"

#include "raylib.h"  // Vector2, Rectangle only

#include <cstdint>
#include <span>
#include <vector>

enum class Click : uint8_t { Left, Right, ShiftLeft };

// Left: pick up the slot into an empty cursor, put the cursor into an empty slot, merge into the
// same stackable item (same id and damage) up to max_stack, else swap. Right: pick up half
// (rounded up) into an empty cursor, or put one into an empty slot or onto the same stackable
// item with room; a different item is left alone. ShiftLeft is the caller's quick move: nothing.
void apply_click(ItemStack& cursor, ItemStack& slot, Click c);

// Moves `from` into `to`: first onto stacks of the same stackable item, in order, then into empty
// slots in order. False and nothing changed when nothing can move; a partial move leaves the rest.
bool quick_move(ItemStack& from, std::span<ItemStack> to);

// What a slot holds: anything, one armor piece of its slot, or output that can only be taken.
enum class SlotKind : uint8_t { Any, Head, Chest, Legs, Feet, TakeOnly };

bool slot_accepts(SlotKind k, const ItemStack& s);  // an empty stack always (taking out)

// apply_click within the slot's rules: a cursor stack the slot does not accept is never put or
// swapped in. TakeOnly (Left or Right): an empty cursor takes the whole slot; a cursor of the same
// item takes it when the sum fits max_stack; anything else does nothing.
void click_slot(ItemStack& cursor, ItemStack& slot, SlotKind kind, Click c);

// Two presses within 0.3 s (inclusive) are a double tap; the press that completes one starts over.
struct DoubleTap {
    double last = -1.0e9;
    bool press(double now);
};

// Every item the Creative palette offers: block items except AIR and WATER, then STICK..END-1.
std::vector<ItemId> creative_palette();

// A grid of `cols` x `rows` square slots of `size` px with `gap` px between them, row-major from
// `origin` (top left). The index of the slot under `p`, or -1 outside every slot (gaps included).
int grid_slot_at(Vector2 p, Vector2 origin, int cols, int rows, float size, float gap);
Rectangle grid_slot_rect(Vector2 origin, int index, int cols, float size, float gap);

// The first visible row of a list of `rows` rows showing `visible` at once, clamped to
// [0, max(0, rows - visible)].
int clamp_scroll(int scroll, int rows, int visible);
