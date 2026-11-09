#include "screens.hpp"

#include "platform.hpp"
#include "render.hpp"
#include "ui_draw.hpp"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <exception>
#include <string>

namespace {

constexpr int SMOKE_PAUSE_AT = 470;     // the game's smoke frame at which --smoke shows the pause menu
constexpr int SMOKE_PAUSE_FRAMES = 12;  // half on the pause menu, half on its settings screen
constexpr int SMOKE_MENU_FRAMES = 8;    // Title, Settings, World list, Create world: 2 frames each

void menu_background() {
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), Color{70, 120, 170, 255}, Color{25, 35, 55, 255});
}

void dim() {
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 140});
}

std::string percent(float v) {
    return std::to_string(static_cast<int>(std::lround(v * 100.f))) + "%";
}

}  // namespace

App::App(const CliOptions& o) : opts_(o) {
    if (!o.smoke) {
        const std::filesystem::path home = user_data_dir();
        settings_file_ = home / "settings.ini";
        saves_dir_ = home / "saves";
        settings_ = load_settings(settings_file_);
    }
    // --smoke paces itself at 60 FPS instead of vsync: a compositor throttles a vsynced window it
    // considers hidden to about 1 frame per second, which would measure the desktop, not the game.
    // It also keeps running if the compositor iconifies the window; raylib otherwise blocks in
    // EndDrawing until the next window event, and the smoke run never finishes.
    unsigned flags = static_cast<unsigned>(FLAG_MSAA_4X_HINT) | static_cast<unsigned>(FLAG_WINDOW_RESIZABLE);
    if (o.smoke) flags |= static_cast<unsigned>(FLAG_WINDOW_ALWAYS_RUN);
    else if (settings_.vsync) flags |= static_cast<unsigned>(FLAG_VSYNC_HINT);
    SetConfigFlags(flags);
    InitWindow(1280, 720, "Nimecraft");
    window_open_ = true;
    SetWindowMinSize(640, 400);
    Image icon = generate_icon(64);  // taskbar, Alt+Tab, and title bar (Linux and Windows)
    SetWindowIcon(icon);
    UnloadImage(icon);
    if (o.smoke) SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    audio_.init();
    // --smoke plays every sound path at master volume 0.
    audio_.set_volumes(o.smoke ? 0.f : settings_.master, settings_.sfx, settings_.music);

    name_field_.text = "New World";
    name_field_.cursor = name_field_.text.size();
    if (o.smoke) return;  // smoke_menus() walks the menus, then starts the game
    if (o.skip_title) {
        // The M1 command line: straight into the world; a world that cannot open ends the program.
        menu_.screen = Screen::Loading;
        game_ = std::make_unique<Game>(o, audio_, settings_);
        return;
    }
    refresh_worlds();
}

App::~App() {
    game_.reset();  // frees its GPU resources while the window still exists
    audio_.shutdown();
    if (window_open_) CloseWindow();
}

float App::factor() const {
    return gui_factor(settings_.gui_scale);
}

Rectangle App::column_rect(int row, float top) const {
    const float f = factor();
    const float w = UI_BUTTON_W * f, h = UI_BUTTON_H * f;
    return Rectangle{std::floor((static_cast<float>(GetScreenWidth()) - w) / 2),
                     top + static_cast<float>(row) * (h + UI_ROW_GAP * f), w, h};
}

void App::act(MenuAction a) {
    if (menu_.apply(a)) audio_.play_ui(Sfx::Click);
}

void App::refresh_worlds() {
    worlds_ = saves_dir_.empty() ? std::vector<WorldInfo>{} : list_worlds(saves_dir_);
    selected_ = worlds_.empty() ? -1 : std::clamp(selected_, 0, static_cast<int>(worlds_.size()) - 1);
}

void App::apply_window_settings() {
    if (opts_.smoke) return;
    if (settings_.vsync) {
        SetWindowState(FLAG_VSYNC_HINT);
        SetTargetFPS(0);
    } else {
        ClearWindowState(FLAG_VSYNC_HINT);
        SetTargetFPS(0);  // uncapped
    }
}

bool App::start_game(const CliOptions& o) {
    try {
        game_ = std::make_unique<Game>(o, audio_, settings_);
        message_.clear();
        return true;
    } catch (const std::exception& e) {
        game_.reset();
        message_ = std::string("Could not open the world: ") + e.what();
        std::fprintf(stderr, "nimecraft: %s\n", message_.c_str());
        return false;
    }
}

void App::end_game() {
    if (!game_) return;
    const int code = game_->finish();
    game_.reset();
    if (code != 0) {
        exit_code_ = code;
        message_ = opts_.smoke ? "Smoke run failed" : "Could not save the world (see the console)";
    }
    if (!opts_.smoke) EnableCursor();
}

int App::run() {
    while (!menu_.quit) {
        if (WindowShouldClose()) break;
        audio_.update(GetFrameTime());
        if (opts_.smoke) smoke_menus();
        BeginDrawing();
        switch (menu_.screen) {
        case Screen::Title: frame_title(); break;
        case Screen::WorldList: frame_world_list(); break;
        case Screen::CreateWorld: frame_create(); break;
        case Screen::ConfirmDelete: frame_confirm_delete(); break;
        case Screen::Loading:
        case Screen::Playing:
        case Screen::Death: frame_game(false); break;
        case Screen::Paused:
            frame_game(true);
            frame_pause_overlay();
            break;
        case Screen::Settings:
            if (game_) frame_game(true);
            frame_settings();
            break;
        }
        EndDrawing();
    }
    end_game();  // the window closed (or --smoke finished) with a world open: save it
    return exit_code_;
}

// --smoke: two frames of each menu screen, then the game; during the game, the pause menu and its
// settings screen for a few frames. Nothing is clicked, so no file is touched.
void App::smoke_menus() {
    if (!game_ && smoke_menu_frame_ < SMOKE_MENU_FRAMES) {
        constexpr Screen order[] = {Screen::Title, Screen::Settings, Screen::WorldList, Screen::CreateWorld};
        menu_.screen = order[smoke_menu_frame_ / 2];
        menu_.settings_return = Screen::Title;
        ++smoke_menu_frame_;
        return;
    }
    if (!game_ && smoke_menu_frame_ == SMOKE_MENU_FRAMES) {
        ++smoke_menu_frame_;
        menu_.screen = Screen::Loading;
        if (!start_game(opts_)) {
            exit_code_ = 1;
            menu_.quit = true;
        }
        return;
    }
    if (!game_ || smoke_paused_done_ || game_->smoke_frames() < SMOKE_PAUSE_AT) return;
    if (smoke_pause_frames_ == 0) menu_.screen = Screen::Paused;
    if (smoke_pause_frames_ == SMOKE_PAUSE_FRAMES / 2) {
        menu_.settings_return = Screen::Paused;
        menu_.screen = Screen::Settings;
    }
    if (++smoke_pause_frames_ > SMOKE_PAUSE_FRAMES) {
        menu_.screen = Screen::Playing;
        smoke_paused_done_ = true;
    }
}

void App::frame_title() {
    menu_background();
    const float f = factor();
    const int big = static_cast<int>(64.f * f);
    const float h = static_cast<float>(GetScreenHeight());
    ui_title("Nimecraft", h * 0.18f, big, WHITE);
    const float top = h * 0.42f;
    if (ui_button(column_rect(0, top), "Play", settings_.gui_scale)) {
        refresh_worlds();
        act(MenuAction::Play);
    }
    if (ui_button(column_rect(1, top), "Settings", settings_.gui_scale)) act(MenuAction::OpenSettings);
    if (ui_button(column_rect(2, top), "Quit", settings_.gui_scale)) act(MenuAction::Quit);
    if (!message_.empty()) ui_title(message_.c_str(), column_rect(3, top).y + 10.f * f, ui_font(settings_.gui_scale), ORANGE);
    const int small = ui_font(1);
    DrawText("Nimecraft " NIMECRAFT_VERSION, 10, GetScreenHeight() - small - 8, small, Color{220, 220, 230, 200});
}

void App::frame_world_list() {
    menu_background();
    const float f = factor();
    const int gs = settings_.gui_scale;
    const float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    ui_title("Select World", 24.f * f, static_cast<int>(36.f * f), WHITE);
    const float list_w = std::min(640.f * f, w - 32.f);
    const float btn_h = UI_BUTTON_H * f, gap = UI_ROW_GAP * f;
    const float list_top = 80.f * f, list_bottom = h - 2 * btn_h - 3 * gap - 16.f;
    const Rectangle list{std::floor((w - list_w) / 2), list_top, list_w, std::max(btn_h, list_bottom - list_top)};
    std::vector<std::string> rows;
    for (const WorldInfo& wi : worlds_)
        rows.push_back(wi.name + "   " + (wi.mode == GameMode::Creative ? "Creative" : "Survival") + "   seed " +
                       std::to_string(wi.seed));
    if (rows.empty()) ui_title("No worlds yet - create one", list.y + list.height / 2 - 10.f * f, ui_font(gs), LIGHTGRAY);
    const int clicked = ui_scroll_list(list, rows, scroll_, selected_, gs);
    if (clicked >= 0) selected_ = clicked;

    // Two rows of two buttons under the list.
    const float bw = (list_w - gap) / 2;
    const float y0 = list.y + list.height + gap, y1 = y0 + btn_h + gap;
    const bool has = selected_ >= 0 && selected_ < static_cast<int>(worlds_.size());
    if (ui_button(Rectangle{list.x, y0, bw, btn_h}, "Play Selected World", gs, has)) {
        act(MenuAction::Play);
        CliOptions g;
        g.world = worlds_[static_cast<std::size_t>(selected_)].folder;
        g.rd = settings_.render_distance;
        if (!start_game(g)) menu_.apply(MenuAction::Back);
    }
    if (ui_button(Rectangle{list.x + bw + gap, y0, bw, btn_h}, "Create New World", gs)) {
        name_field_.text = "New World";
        name_field_.cursor = name_field_.text.size();
        seed_field_ = TextField{};
        seed_field_.max_len = 32;
        name_focus_ = true;
        seed_focus_ = false;
        create_creative_ = false;
        folder_note_.clear();
        act(MenuAction::Create);
    }
    if (ui_button(Rectangle{list.x, y1, bw, btn_h}, "Delete", gs, has)) act(MenuAction::Delete);
    if (ui_button(Rectangle{list.x + bw + gap, y1, bw, btn_h}, "Back", gs) || IsKeyPressed(KEY_ESCAPE)) act(MenuAction::Back);
    if (!message_.empty()) ui_title(message_.c_str(), y1 + btn_h + 4.f, ui_font(1), ORANGE);
}

void App::frame_create() {
    menu_background();
    const float f = factor();
    const int gs = settings_.gui_scale, font = ui_font(gs);
    const float h = static_cast<float>(GetScreenHeight());
    ui_title("Create World", 24.f * f, static_cast<int>(36.f * f), WHITE);
    const float top = h * 0.22f;
    const Rectangle name_r = column_rect(1, top), seed_r = column_rect(3, top);
    DrawText("World name", static_cast<int>(name_r.x), static_cast<int>(name_r.y) - font - 4, font, LIGHTGRAY);
    // The folder the name maps to; the saves folder is scanned again only when the name changes.
    if (ui_text_field(name_r, name_field_, name_focus_, gs) || folder_note_.empty())
        folder_note_ = "Folder: " + (saves_dir_.empty() ? sanitize_world_name(name_field_.text)
                                                        : unique_folder(saves_dir_, name_field_.text));
    const std::string& folder_note = folder_note_;
    DrawText(folder_note.c_str(), static_cast<int>(name_r.x), static_cast<int>(name_r.y + name_r.height) + 4,
             ui_font(1), GRAY);
    DrawText("Seed (blank for random)", static_cast<int>(seed_r.x), static_cast<int>(seed_r.y) - font - 4, font,
             LIGHTGRAY);
    ui_text_field(seed_r, seed_field_, seed_focus_, gs);
    if (IsKeyPressed(KEY_TAB)) std::swap(name_focus_, seed_focus_);
    const std::string mode = std::string("Mode: ") + (create_creative_ ? "Creative" : "Survival");
    if (ui_button(column_rect(4, top + 10.f * f), mode.c_str(), gs)) {
        create_creative_ = !create_creative_;
        audio_.play_ui(Sfx::Click);
    }
    const bool create = ui_button(column_rect(5, top + 10.f * f), "Create", gs) || IsKeyPressed(KEY_ENTER);
    if (ui_button(column_rect(6, top + 10.f * f), "Cancel", gs) || IsKeyPressed(KEY_ESCAPE)) {
        act(MenuAction::Back);
        return;
    }
    if (!create || opts_.smoke) return;
    CliOptions g;
    g.world = unique_folder(saves_dir_, name_field_.text);
    g.seed = seed_from_text(seed_field_.text, static_cast<uint32_t>(std::time(nullptr)));
    g.creative = create_creative_;
    g.rd = settings_.render_distance;
    act(MenuAction::Create);
    if (!start_game(g)) {
        menu_.apply(MenuAction::Back);
        refresh_worlds();
    }
}

void App::frame_confirm_delete() {
    menu_background();
    const float f = factor();
    const int gs = settings_.gui_scale;
    const float h = static_cast<float>(GetScreenHeight());
    const bool has = selected_ >= 0 && selected_ < static_cast<int>(worlds_.size());
    const std::string name = has ? worlds_[static_cast<std::size_t>(selected_)].name : std::string("?");
    const std::string q = "Delete \"" + name + "\"?";
    ui_title(q.c_str(), h * 0.3f, static_cast<int>(32.f * f), WHITE);
    ui_title("The world and everything in it will be gone for good.", h * 0.3f + 44.f * f, ui_font(gs), LIGHTGRAY);
    if (ui_button(column_rect(0, h * 0.5f), "Delete", gs, has)) {
        if (!delete_world(saves_dir_, worlds_[static_cast<std::size_t>(selected_)].folder))
            message_ = "Could not delete \"" + name + "\"";
        else
            message_.clear();
        act(MenuAction::Confirm);
        refresh_worlds();
        return;
    }
    if (ui_button(column_rect(1, h * 0.5f), "Cancel", gs) || IsKeyPressed(KEY_ESCAPE)) act(MenuAction::Back);
}

void App::frame_game(bool paused) {
    if (!game_) {  // nothing to show (the world failed to open): back to the menus
        menu_.screen = Screen::Title;
        return;
    }
    if (!game_->frame(paused)) {  // --smoke finished
        end_game();
        menu_.quit = true;
        return;
    }
    if (paused) return;
    if (menu_.screen == Screen::Loading && game_->spawned()) menu_.apply(MenuAction::Loaded);
    if (menu_.screen == Screen::Playing && game_->dead()) menu_.apply(MenuAction::Died);
    if (menu_.screen == Screen::Death && !game_->dead()) menu_.apply(MenuAction::Respawn);
    if (game_->take_pause_request() && menu_.apply(MenuAction::Pause)) game_->set_paused(true);
    if (game_->take_quit_request() && menu_.apply(MenuAction::SaveAndQuit)) {
        end_game();
        refresh_worlds();
    }
}

void App::frame_pause_overlay() {
    dim();
    const float h = static_cast<float>(GetScreenHeight());
    const int gs = settings_.gui_scale;
    ui_title("Paused", h * 0.22f, static_cast<int>(40.f * factor()), WHITE);
    const float top = h * 0.38f;
    if (ui_button(column_rect(0, top), "Resume", gs) || IsKeyPressed(KEY_ESCAPE)) {
        act(MenuAction::Resume);
        if (game_) game_->set_paused(false);
        return;
    }
    if (ui_button(column_rect(1, top), "Settings", gs)) act(MenuAction::OpenSettings);
    if (ui_button(column_rect(2, top), "Save and Quit to Title", gs)) {
        act(MenuAction::SaveAndQuit);
        end_game();
        refresh_worlds();
    }
}

void App::frame_settings() {
    if (game_) dim();
    else menu_background();
    const float f = factor();
    const int gs = settings_.gui_scale;
    const float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    ui_title("Settings", 24.f * f, static_cast<int>(36.f * f), WHITE);
    const float col_w = std::min(UI_BUTTON_W * f, (w - 48.f) / 2), row_h = UI_BUTTON_H * f, gap = UI_ROW_GAP * f;
    const float left = std::floor(w / 2 - col_w - gap), right = std::floor(w / 2 + gap);
    const float top = std::max(80.f * f, h * 0.2f);
    const auto at = [&](float x, int row) {
        return Rectangle{x, top + static_cast<float>(row) * (row_h + gap), col_w, row_h};
    };

    float rd = static_cast<float>(settings_.render_distance);
    if (ui_slider(at(left, 0), "Render distance", std::to_string(settings_.render_distance).c_str(), rd,
                  static_cast<float>(RD_MIN), static_cast<float>(RD_MAX), 1.f, gs))
        settings_.render_distance = static_cast<int>(std::lround(rd));
    float fov = static_cast<float>(settings_.fov);
    if (ui_slider(at(left, 1), "Field of view", std::to_string(settings_.fov).c_str(), fov,
                  static_cast<float>(FOV_MIN), static_cast<float>(FOV_MAX), 1.f, gs))
        settings_.fov = static_cast<int>(std::lround(fov));
    char sens[16];
    std::snprintf(sens, sizeof sens, "%.1fx", static_cast<double>(settings_.sensitivity));
    ui_slider(at(left, 2), "Mouse sensitivity", sens, settings_.sensitivity, SENS_MIN, SENS_MAX, 0.1f, gs);
    float gui = static_cast<float>(settings_.gui_scale);
    if (ui_slider(at(left, 3), "GUI scale", std::to_string(settings_.gui_scale).c_str(), gui,
                  static_cast<float>(GUI_MIN), static_cast<float>(GUI_MAX), 1.f, gs))
        settings_.gui_scale = static_cast<int>(std::lround(gui));

    bool volume = false;
    volume |= ui_slider(at(right, 0), "Master volume", percent(settings_.master).c_str(), settings_.master, 0.f, 1.f, 0.05f, gs);
    volume |= ui_slider(at(right, 1), "Sound effects", percent(settings_.sfx).c_str(), settings_.sfx, 0.f, 1.f, 0.05f, gs);
    volume |= ui_slider(at(right, 2), "Music", percent(settings_.music).c_str(), settings_.music, 0.f, 1.f, 0.05f, gs);
    if (volume && !opts_.smoke) audio_.set_volumes(settings_.master, settings_.sfx, settings_.music);
    if (ui_toggle(at(right, 3), "VSync", settings_.vsync, gs)) apply_window_settings();

    const Rectangle done{std::floor((w - col_w) / 2), top + 4.f * (row_h + gap) + gap, col_w, row_h};
    if (!ui_button(done, "Done", gs) && !IsKeyPressed(KEY_ESCAPE)) return;
    settings_ = clamp_settings(settings_);
    if (!settings_file_.empty() && !save_settings(settings_file_, settings_)) message_ = "Could not save settings.ini";
    if (game_) game_->apply_settings(settings_);
    act(MenuAction::Back);
}
