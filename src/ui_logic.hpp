// Window-free inventory UI rules: slot clicks, quick move, slot kinds, double tap, the Creative
// palette, and slot grid geometry. Logic only: raylib for Vector2 and Rectangle.
#pragma once

#include "items.hpp"

#include "raylib.h"  // Vector2, Rectangle only

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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

// One icon of a 10-icon survival bar (health, hunger, air, armor) showing a 0..20 value in half
// steps: icon i is Full at value >= 2i + 2, Half at value > 2i, else Empty (NaN: Empty).
enum class IconFill : uint8_t { Empty, Half, Full };
IconFill icon_fill(float value, int i);

// Every item the Creative palette offers: block items except AIR, WATER, and GRAVE_CAIRN (made
// only by dying), then STICK..END-1.
std::vector<ItemId> creative_palette();

// A grid of `cols` x `rows` square slots of `size` px with `gap` px between them, row-major from
// `origin` (top left). The index of the slot under `p`, or -1 outside every slot (gaps included).
int grid_slot_at(Vector2 p, Vector2 origin, int cols, int rows, float size, float gap);
Rectangle grid_slot_rect(Vector2 origin, int index, int cols, float size, float gap);

// The first visible row of a list of `rows` rows showing `visible` at once, clamped to
// [0, max(0, rows - visible)].
int clamp_scroll(int scroll, int rows, int visible);

// ---- Menu widgets (M8 Task 3) ----

// Code points in UTF-8 text: every byte that is not a continuation byte (10xxxxxx) starts one.
std::size_t utf8_length(std::string_view s);

// A one-line UTF-8 text box. `cursor` is a byte offset that always sits on a character boundary;
// max_len counts characters, not bytes. insert() refuses control characters (below 0x20 and 0x7F),
// surrogates, code points above 0x10FFFF, and any character once the text holds max_len; left and
// right move one whole character and stop at the ends; backspace removes the whole character
// before the cursor (nothing at 0).
struct TextField {
    std::string text;
    std::size_t cursor = 0;
    std::size_t max_len = 32;
    void insert(char32_t c);
    void backspace();
    void left();
    void right();
};

// Widget size multiplier for settings.gui_scale 1..4: 0.75, 1, 1.25, 1.5 (out of range clamped).
float gui_factor(int gui_scale);

// Slider math. The value under mouse x on a horizontal track: lo at the left edge, hi at the right,
// clamped, and snapped to the nearest multiple of `step` above lo when step > 0. A track with no
// width or a non-finite x gives lo. slider_fraction is the knob position 0..1 of v (clamped; 0 when
// hi <= lo).
float slider_value_at(float x, Rectangle track, float lo, float hi, float step);
float slider_fraction(float v, float lo, float hi);

// The row of a scroll list under p: `area` shows rows of row_h px starting with row `scroll`;
// the index into the full list, or -1 outside the area, past the last of `count` rows, or for a
// row_h <= 0.
int list_row_at(Vector2 p, Rectangle area, float row_h, int scroll, int count);

// ---- Menu flow (M8 Task 4) ----
// Title (Play, Settings, Quit) -> WorldList (Play, Create, Delete, Back) -> CreateWorld (Create,
// Back) -> Loading (Loaded; Back when the world fails to open) -> Playing (Pause, Died);
// ConfirmDelete (Confirm, Back) returns to WorldList; Paused (Resume, Back, Settings, Save and
// Quit to Title); Settings (Back to the screen that opened it: Title or Paused); Death (Respawn,
// Save and Quit to Title). Quit on the Title sets `quit` and keeps the screen.

enum class Screen : uint8_t { Title, WorldList, CreateWorld, ConfirmDelete, Loading, Playing, Paused, Settings, Death };
enum class MenuAction : uint8_t {
    Play, Create, Delete, Confirm, Back, OpenSettings, Quit, Loaded, Pause, Resume, SaveAndQuit, Died, Respawn
};

struct MenuState {
    Screen screen = Screen::Title;
    Screen settings_return = Screen::Title;  // where Settings goes back to
    bool quit = false;
    // Applies an action the current screen offers; false, and nothing changes, for any other.
    bool apply(MenuAction a);
};
