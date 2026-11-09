#include "ui_draw.hpp"

#include "block.hpp"
#include "render.hpp"

#include <algorithm>
#include <string>

void draw_item_icon(const UiTextures& t, const ItemStack& s, Rectangle dst) {
    if (s.empty()) return;
    const Texture2D tex = s.id < blk::COUNT ? t.blocks : t.items;
    DrawTexturePro(tex, Renderer::icon_rect(s.id), dst, Vector2{0, 0}, 0.f, WHITE);
    const ItemDef& d = item_def(s.id);
    if (s.count > 1) {
        const std::string n = std::to_string(s.count);
        const int size = std::max(10, static_cast<int>(dst.height * 0.4f));
        const int w = MeasureText(n.c_str(), size);
        const int x = static_cast<int>(dst.x + dst.width) - w - 1, y = static_cast<int>(dst.y + dst.height) - size;
        DrawText(n.c_str(), x + 1, y + 1, size, BLACK);
        DrawText(n.c_str(), x, y, size, WHITE);
    }
    if (d.durability > 0 && s.damage > 0) {
        const float left = 1.f - static_cast<float>(s.damage) / static_cast<float>(d.durability);
        const Rectangle bar{dst.x + 2, dst.y + dst.height - 4, dst.width - 4, 3};
        DrawRectangleRec(bar, BLACK);
        const Color c = left > 0.5f ? GREEN : (left > 0.2f ? YELLOW : RED);
        DrawRectangleRec(Rectangle{bar.x, bar.y, bar.width * left, bar.height}, c);
    }
}

void draw_slot(const UiTextures& t, const ItemStack& s, Rectangle r, bool selected, bool hovered) {
    DrawRectangleRec(r, UI_SLOT_BG);
    draw_item_icon(t, s, Rectangle{r.x + 4, r.y + 4, r.width - 8, r.height - 8});
    if (hovered) DrawRectangleRec(r, UI_HOVER);
    if (selected) DrawRectangleLinesEx(Rectangle{r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 2.f, WHITE);
}

void draw_panel(Rectangle r) {
    DrawRectangleRec(r, UI_PANEL);
    DrawRectangleLinesEx(r, 1.f, Color{90, 90, 100, 255});
}

void draw_label(const std::string& text, float x, float y, int size, Color c) {
    DrawText(text.c_str(), static_cast<int>(x), static_cast<int>(y), size, c);
}

void draw_bar(Rectangle r, float fraction, Color fill) {
    DrawRectangleRec(r, Color{0, 0, 0, 150});
    DrawRectangleRec(Rectangle{r.x, r.y, r.width * std::clamp(fraction, 0.f, 1.f), r.height}, fill);
}

void draw_tooltip(const std::string& text, Vector2 at) {
    constexpr int SIZE = 16;
    const int w = MeasureText(text.c_str(), SIZE);
    const Rectangle r{at.x + 14, at.y - 6, static_cast<float>(w + 10), SIZE + 8};
    DrawRectangleRec(r, Color{10, 10, 20, 235});
    DrawText(text.c_str(), static_cast<int>(r.x) + 5, static_cast<int>(r.y) + 4, SIZE, WHITE);
}

void draw_hotbar(const UiTextures& t, const Inventory& inv) {
    constexpr float SLOT = 52.f, GAP = 4.f;
    const float total = 9 * SLOT + 8 * GAP;
    const float x0 = (static_cast<float>(GetScreenWidth()) - total) / 2, y0 = static_cast<float>(GetScreenHeight()) - SLOT - 10;
    for (int i = 0; i < 9; ++i) {
        const Rectangle r{x0 + static_cast<float>(i) * (SLOT + GAP), y0, SLOT, SLOT};
        draw_slot(t, inv.slots[static_cast<std::size_t>(i)], r, i == inv.selected, false);
        DrawText(std::to_string(i + 1).c_str(), static_cast<int>(r.x) + 3, static_cast<int>(r.y) + 2, 10, LIGHTGRAY);
    }
    const ItemStack& held = inv.held();
    if (!held.empty()) {
        const std::string name = item_label(held);
        DrawText(name.c_str(), (GetScreenWidth() - MeasureText(name.c_str(), 18)) / 2, static_cast<int>(y0) - 24, 18,
                 WHITE);
    }
}

std::string item_label(const ItemStack& s) {
    if (s.empty()) return {};
    const ItemDef& d = item_def(s.id);
    std::string out(d.name);
    if (d.durability > 0) out += " (" + std::to_string(d.durability - s.damage) + "/" + std::to_string(d.durability) + ")";
    return out;
}
