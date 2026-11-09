#include "ui_draw.hpp"

#include "block.hpp"
#include "render.hpp"
#include "ui_logic.hpp"

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
    constexpr float SLOT = 52.f, GAP = 4.f;  // HOTBAR_SLOT and HOTBAR_GAP below must match
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

// ---- Survival HUD (M5 Task 4) ----

namespace {

constexpr float HUD_PX = 2.f;  // screen pixels per icon pixel
constexpr int ICON = 9;        // icon art is 9 x 9
constexpr float ICON_STEP = ICON * HUD_PX + 2.f;
constexpr float HOTBAR_SLOT = 52.f, HOTBAR_GAP = 4.f, HOTBAR_BOTTOM = 10.f;

// Icon art: '#' body, '+' highlight, '.' empty.
constexpr const char* HEART[ICON] = {".##...##.", "#+##.####", "#+#######", "#########", ".#######.",
                                     "..#####..", "...###...", "....#....", "........."};
constexpr const char* DRUMSTICK[ICON] = {"....####.", "...#+####", "...######", "...######", "....####.",
                                         "...##....", "..##.....", ".##......", "##......."};
constexpr const char* BUBBLE[ICON] = {".........", "...###...", "..#+..#..", ".#+....#.", ".#.....#.",
                                      ".#.....#.", "..#...#..", "...###...", "........."};
constexpr const char* SHIELD[ICON] = {"#########", "#+#######", "#+#######", "#########", "#########",
                                      ".#######.", ".#######.", "..#####..", "...###..."};

// One icon at (x, y). Full: body color; Half: the left half in color, the rest dark; Empty: dark.
void draw_icon(const char* const* art, float x, float y, IconFill fill, Color body) {
    const Color dark{30, 24, 24, 200}, light{255, 255, 255, 220};
    for (int r = 0; r < ICON; ++r)
        for (int c = 0; c < ICON && art[r][c] != '\0'; ++c) {
            const char p = art[r][c];
            if (p == '.') continue;
            const bool lit = fill == IconFill::Full || (fill == IconFill::Half && c < ICON / 2 + 1);
            const Color col = !lit ? dark : (p == '+' ? light : body);
            DrawRectangleRec(Rectangle{x + c * HUD_PX, y + r * HUD_PX, HUD_PX, HUD_PX}, col);
        }
}

// 10 icons for a 0..20 value, left to right from x (or right to left when mirrored).
void draw_icon_row(const char* const* art, float x, float y, float value, Color body, bool from_right) {
    for (int i = 0; i < 10; ++i) {
        const float ix = from_right ? x - (i + 1) * ICON_STEP : x + i * ICON_STEP;
        draw_icon(art, ix, y, icon_fill(value, i), body);
    }
}

float hotbar_left() {
    return (static_cast<float>(GetScreenWidth()) - (9 * HOTBAR_SLOT + 8 * HOTBAR_GAP)) / 2;
}

float hotbar_top() {
    return static_cast<float>(GetScreenHeight()) - HOTBAR_SLOT - HOTBAR_BOTTOM;
}

}  // namespace

void draw_survival_hud(const Vitals& v, int armor, bool show_air) {
    const float left = hotbar_left(), right = left + 9 * HOTBAR_SLOT + 8 * HOTBAR_GAP;
    const float row1 = hotbar_top() - 46.f - ICON * HUD_PX, row2 = row1 - ICON_STEP;
    draw_icon_row(HEART, left, row1, v.health, Color{214, 40, 48, 255}, false);
    draw_icon_row(DRUMSTICK, right, row1, v.hunger, Color{196, 128, 70, 255}, true);
    if (armor > 0) draw_icon_row(SHIELD, left, row2, static_cast<float>(armor), Color{196, 200, 210, 255}, false);
    if (show_air) draw_icon_row(BUBBLE, right, row2, v.air * 2.f, Color{120, 190, 255, 255}, true);
}

void draw_hurt_flash(float strength) {
    const float a = std::clamp(strength, 0.f, 1.f);
    if (a <= 0.f) return;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{200, 0, 0, static_cast<unsigned char>(a * 110.f)});
}

Rectangle death_button_rect() {
    constexpr float W = 220.f, H = 44.f;
    return Rectangle{(static_cast<float>(GetScreenWidth()) - W) / 2, static_cast<float>(GetScreenHeight()) / 2 + 10, W, H};
}

Rectangle death_quit_rect() {
    const Rectangle r = death_button_rect();
    return Rectangle{r.x, r.y + r.height + 12, r.width, r.height};
}

void draw_death_screen(const std::string& note) {
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{90, 0, 0, 150});
    const char* title = "You died";
    DrawText(title, (GetScreenWidth() - MeasureText(title, 48)) / 2, GetScreenHeight() / 2 - 80, 48, WHITE);
    if (!note.empty())
        DrawText(note.c_str(), (GetScreenWidth() - MeasureText(note.c_str(), 18)) / 2, GetScreenHeight() / 2 - 26, 18,
                 LIGHTGRAY);
    const Vector2 m = GetMousePosition();
    for (const auto& [b, label] : {std::pair{death_button_rect(), "Respawn"}, std::pair{death_quit_rect(), "Quit to Title"}}) {
        DrawRectangleRec(b, CheckCollisionPointRec(m, b) ? Color{110, 110, 130, 240} : Color{60, 60, 74, 240});
        DrawRectangleLinesEx(b, 2.f, WHITE);
        DrawText(label, static_cast<int>(b.x + (b.width - static_cast<float>(MeasureText(label, 24))) / 2),
                 static_cast<int>(b.y) + 10, 24, WHITE);
    }
}

void draw_toast(const std::string& text) {
    if (text.empty()) return;
    constexpr int SIZE = 20;
    const int w = MeasureText(text.c_str(), SIZE);
    const int y = static_cast<int>(hotbar_top()) - 110;
    DrawRectangle((GetScreenWidth() - w) / 2 - 8, y - 4, w + 16, SIZE + 8, Color{0, 0, 0, 150});
    DrawText(text.c_str(), (GetScreenWidth() - w) / 2, y, SIZE, WHITE);
}

// ---- Menu widgets (M8 Task 3) ----

namespace {

constexpr Color WIDGET{60, 60, 72, 235}, WIDGET_HOT{90, 90, 115, 245}, WIDGET_OFF{45, 45, 50, 200},
                WIDGET_EDGE{140, 140, 160, 255}, TRACK_FILL{110, 160, 90, 255};

const void* g_drag = nullptr;  // the slider value being dragged, if any

bool hovered(Rectangle r) {
    return CheckCollisionPointRec(GetMousePosition(), r);
}

void centered_text(const char* text, Rectangle r, int size, Color c) {
    const int w = MeasureText(text, size);
    DrawText(text, static_cast<int>(r.x + (r.width - static_cast<float>(w)) / 2),
             static_cast<int>(r.y + (r.height - static_cast<float>(size)) / 2), size, c);
}

}  // namespace

int ui_font(int gui_scale) {
    return static_cast<int>(20.f * gui_factor(gui_scale) + 0.5f);
}

void ui_title(const char* text, float y, int size, Color c) {
    DrawText(text, (GetScreenWidth() - MeasureText(text, size)) / 2, static_cast<int>(y), size, c);
}

bool ui_button(Rectangle r, const char* label, int gui_scale, bool enabled) {
    const bool over = enabled && hovered(r);
    DrawRectangleRec(r, !enabled ? WIDGET_OFF : (over ? WIDGET_HOT : WIDGET));
    DrawRectangleLinesEx(r, over ? 2.f : 1.f, WIDGET_EDGE);
    centered_text(label, r, ui_font(gui_scale), enabled ? WHITE : GRAY);
    return over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

bool ui_slider(Rectangle r, const char* label, const char* shown, float& value, float lo, float hi, float step,
               int gui_scale) {
    const bool over = hovered(r);
    if (over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_drag = &value;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && g_drag == &value) g_drag = nullptr;
    const float before = value;
    if (g_drag == &value) value = slider_value_at(GetMousePosition().x, r, lo, hi, step);
    DrawRectangleRec(r, over || g_drag == &value ? WIDGET_HOT : WIDGET);
    DrawRectangleRec(Rectangle{r.x, r.y, r.width * slider_fraction(value, lo, hi), r.height},
                     Color{TRACK_FILL.r, TRACK_FILL.g, TRACK_FILL.b, 120});
    const float knob_w = 8.f * gui_factor(gui_scale);
    const float kx = r.x + (r.width - knob_w) * slider_fraction(value, lo, hi);
    DrawRectangleRec(Rectangle{kx, r.y, knob_w, r.height}, WIDGET_EDGE);
    DrawRectangleLinesEx(r, 1.f, WIDGET_EDGE);
    const std::string text = std::string(label) + ": " + shown;
    centered_text(text.c_str(), r, ui_font(gui_scale), WHITE);
    return value != before;
}

bool ui_toggle(Rectangle r, const char* label, bool& value, int gui_scale) {
    const std::string text = std::string(label) + ": " + (value ? "On" : "Off");
    if (!ui_button(r, text.c_str(), gui_scale)) return false;
    value = !value;
    return true;
}

bool ui_text_field(Rectangle r, TextField& f, bool& focused, int gui_scale) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) focused = hovered(r);
    bool changed = false;
    if (focused) {
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed()) {
            const std::string before = f.text;
            f.insert(static_cast<char32_t>(c));
            changed = changed || f.text != before;
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            const std::size_t before = f.text.size();
            f.backspace();
            changed = changed || f.text.size() != before;
        }
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) f.left();
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) f.right();
        if (IsKeyPressed(KEY_HOME)) f.cursor = 0;
        if (IsKeyPressed(KEY_END)) f.cursor = f.text.size();
    }
    DrawRectangleRec(r, Color{20, 20, 26, 240});
    DrawRectangleLinesEx(r, focused ? 2.f : 1.f, focused ? WHITE : WIDGET_EDGE);
    const int size = ui_font(gui_scale);
    const int pad = static_cast<int>(8.f * gui_factor(gui_scale));
    const int ty = static_cast<int>(r.y + (r.height - static_cast<float>(size)) / 2);
    DrawText(f.text.c_str(), static_cast<int>(r.x) + pad, ty, size, WHITE);
    if (focused && static_cast<int>(GetTime() * 2.0) % 2 == 0) {  // blinking caret
        const std::string head = f.text.substr(0, std::min(f.cursor, f.text.size()));
        const int cx = static_cast<int>(r.x) + pad + MeasureText(head.c_str(), size) + 1;
        DrawRectangle(cx, ty, 2, size, WHITE);
    }
    return changed;
}

int ui_scroll_list(Rectangle r, const std::vector<std::string>& rows, int& scroll, int selected, int gui_scale) {
    const float row_h = 34.f * gui_factor(gui_scale);
    const int visible = std::max(1, static_cast<int>(r.height / row_h));
    const int count = static_cast<int>(rows.size());
    const bool over = hovered(r);
    if (over) {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.f) scroll += wheel > 0.f ? -1 : 1;
    }
    scroll = clamp_scroll(scroll, count, visible);
    DrawRectangleRec(r, Color{0, 0, 0, 110});
    DrawRectangleLinesEx(r, 1.f, WIDGET_EDGE);
    const int size = ui_font(gui_scale);
    const Vector2 m = GetMousePosition();
    const int under = over ? list_row_at(m, r, row_h, scroll, count) : -1;
    for (int i = 0; i < visible && scroll + i < count; ++i) {
        const int at = scroll + i;
        const Rectangle row{r.x + 2, r.y + static_cast<float>(i) * row_h + 1, r.width - 4, row_h - 2};
        if (at == selected) DrawRectangleRec(row, Color{80, 110, 70, 220});
        else if (at == under) DrawRectangleRec(row, UI_HOVER);
        DrawText(rows[static_cast<std::size_t>(at)].c_str(), static_cast<int>(row.x + 8.f * gui_factor(gui_scale)),
                 static_cast<int>(row.y + (row.height - static_cast<float>(size)) / 2), size, WHITE);
    }
    if (count > visible) {  // a scroll bar along the right edge
        const float bar_h = r.height * static_cast<float>(visible) / static_cast<float>(count);
        const float bar_y = r.y + (r.height - bar_h) * static_cast<float>(scroll) / static_cast<float>(count - visible);
        DrawRectangleRec(Rectangle{r.x + r.width - 6, bar_y, 4, bar_h}, WIDGET_EDGE);
    }
    return under >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ? under : -1;
}
