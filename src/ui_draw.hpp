// 2D drawing of item icons, slots, panels, and the hotbar. Device module (draws with raylib);
// the rules for clicks and layout math live in ui_logic. Covered by --smoke and the Manual checks.
#pragma once

#include "inventory.hpp"
#include "items.hpp"

#include "raylib.h"

#include <string>

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
