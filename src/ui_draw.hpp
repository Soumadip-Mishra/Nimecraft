// 2D drawing of item icons, slots, panels, and the hotbar. Device module (draws with raylib);
// the rules for clicks and layout math live in ui_logic. Covered by --smoke and the Manual checks.
#pragma once

#include "inventory.hpp"
#include "items.hpp"
#include "vitals.hpp"

#include "raylib.h"

#include <string>
#include <vector>

struct UiTextures {
    Texture2D blocks, items;  // Renderer::block_atlas(), Renderer::item_atlas()
};

constexpr float UI_SLOT = 40.f, UI_GAP = 4.f;
constexpr Color UI_PANEL{30, 30, 36, 230}, UI_SLOT_BG{0, 0, 0, 120}, UI_HOVER{255, 255, 255, 60};

// Icon of s scaled into dst, with its count (when > 1) and a wear bar for damaged tools.
void draw_item_icon(const UiTextures& t, const ItemStack& s, Rectangle dst);
// A slot background, the stack in it, and an outline when `selected`.
void draw_slot(const UiTextures& t, const ItemStack& s, Rectangle r, bool selected, bool hovered);
void draw_panel(Rectangle r);
void draw_label(const std::string& text, float x, float y, int size, Color c);
void draw_bar(Rectangle r, float fraction, Color fill);  // fraction clamped to 0..1
void draw_tooltip(const std::string& text, Vector2 at);
// The 9 hotbar slots of inv along the bottom edge, inv.selected outlined.
void draw_hotbar(const UiTextures& t, const Inventory& inv);
// The item's display name, with "uses left/durability" for tools.
std::string item_label(const ItemStack& s);

// Survival HUD (M5) above the hotbar: 10 hearts (left) and 10 hunger icons (right) in half steps,
// the armor bar above the hearts when armor > 0, and air bubbles above the hunger icons when
// show_air.
void draw_survival_hud(const Vitals& v, int armor, bool show_air);
// A red full-screen flash; strength 0..1 (clamped).
void draw_hurt_flash(float strength);
// The death screen's "Respawn" button, centered.
Rectangle death_button_rect();
// The death screen's "Quit to Title" button, under Respawn (saves and returns to the title screen).
Rectangle death_quit_rect();
// Dims the screen, "You died", a note under it (skipped when empty), and the Respawn and Quit to
// Title buttons (each highlighted while hovered).
void draw_death_screen(const std::string& note);
// A short message centered above the hotbar.
void draw_toast(const std::string& text);

// ---- Menu widgets (M8 Task 3): immediate mode, read the mouse and keyboard themselves ----
// Rectangles are in screen pixels (screens size them with ui_logic's gui_factor); text size and
// padding follow gui_scale (1..4). Each call draws the widget and returns what the player did.

struct TextField;  // ui_logic.hpp

constexpr float UI_BUTTON_W = 300.f, UI_BUTTON_H = 40.f, UI_ROW_GAP = 10.f;  // at gui_factor 1

int ui_font(int gui_scale);  // 20 px at gui_scale 2
// Centered text at y.
void ui_title(const char* text, float y, int size, Color c);
// True on the click that releases inside the button; a disabled button is greyed and never fires.
bool ui_button(Rectangle r, const char* label, int gui_scale, bool enabled = true);
// "label: value" over a track; dragging (press inside, then move while held) sets value to
// slider_value_at(...). `shown` is the value text (e.g. "80%"). True when value changed.
bool ui_slider(Rectangle r, const char* label, const char* shown, float& value, float lo, float hi, float step,
               int gui_scale);
// "label: On/Off"; a click flips value and returns true.
bool ui_toggle(Rectangle r, const char* label, bool& value, int gui_scale);
// A one-line text box: a click inside focuses it, a click elsewhere unfocuses; while focused it
// takes typed characters, Backspace, Left, and Right (with key repeat). True when the text changed.
bool ui_text_field(Rectangle r, TextField& f, bool& focused, int gui_scale);
// A list of rows inside r (scrolled with the wheel while hovered, clamped), `selected` highlighted;
// the index of the row clicked this frame, or -1.
int ui_scroll_list(Rectangle r, const std::vector<std::string>& rows, int& scroll, int selected, int gui_scale);
