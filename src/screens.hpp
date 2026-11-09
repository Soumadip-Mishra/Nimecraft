// The program around a game: the window, audio, settings.ini, and the menus (title, world list,
// create world, delete confirmation, loading, pause, settings; the death screen is drawn by Game).
// The flow itself is MenuState (ui_logic, tested); the world list rules are in saves (tested).
// Device module: covered by --smoke and the Manual checks.
#pragma once

#include "audio.hpp"
#include "cli.hpp"
#include "game.hpp"
#include "saves.hpp"
#include "settings.hpp"
#include "ui_logic.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class App {
public:
    // Loads settings.ini (defaults in --smoke, which reads and writes nothing), opens the window and
    // the audio device. With o.skip_title the game named by o starts at once (M1 command line).
    explicit App(const CliOptions& o);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Runs until Quit or the window closes; a running game is saved first. The exit code: 1 when a
    // save failed (or --smoke failed), else 0.
    int run();

private:
    void frame_title();
    void frame_world_list();
    void frame_create();
    void frame_confirm_delete();
    void frame_game(bool paused);
    void frame_pause_overlay();
    void frame_settings();
    void smoke_menus();

    bool start_game(const CliOptions& o);  // false (and a message) when the world cannot open
    void end_game();                       // saves and closes the running game
    void refresh_worlds();
    void apply_window_settings();
    void act(MenuAction a);  // MenuState::apply plus the click sound
    Rectangle column_rect(int row, float top) const;  // a centered button-sized slot
    float factor() const;

    CliOptions opts_;
    Settings settings_;
    std::filesystem::path settings_file_, saves_dir_;  // empty in --smoke
    Audio audio_;
    MenuState menu_;
    std::unique_ptr<Game> game_;
    int exit_code_ = 0;
    std::string message_;  // shown on the title or world list (a failed save or load)

    // World list.
    std::vector<WorldInfo> worlds_;
    int selected_ = -1, scroll_ = 0;

    // Create world.
    TextField name_field_, seed_field_;
    bool name_focus_ = true, seed_focus_ = false, create_creative_ = false;
    std::string folder_note_;  // "Folder: <name>" for the current name (empty: recompute)

    // --smoke: the menu screens shown before the game starts, and the pause shown during it.
    int smoke_menu_frame_ = 0, smoke_pause_frames_ = 0;
    bool smoke_paused_done_ = false;
    bool window_open_ = false;
};
