// One running game: world load and save, the per-frame loop, input, mining and placing, item
// entities, kilns, the inventory, crafting, kiln, and crate screens, Creative mode, survival
// (vitals, eating, fall damage, death, grave cairns, bedroll respawn, sleeping), and the HUD.
// Device module (window, input, clock): decisions live in the tested logic modules; this class only
// wires them to the window. Covered by --smoke and the Manual checks.
#pragma once

#include "chunk_manager.hpp"
#include "cli.hpp"
#include "combat.hpp"
#include "inventory.hpp"
#include "item_entities.hpp"
#include "jobs.hpp"
#include "player.hpp"
#include "raycast.hpp"
#include "render.hpp"
#include "storage.hpp"
#include "ui_draw.hpp"
#include "ui_logic.hpp"
#include "vitals.hpp"
#include "world.hpp"
#include "world_time.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Game {
public:
    explicit Game(const CliOptions& o);  // loads world.meta and opens the window
    ~Game();                             // closes the window if finish() was not called
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    bool frame();  // one frame; false once the game should quit
    bool save();   // modified columns, regions, and world.meta; false if a write failed
    // Closes any open screen and the window, then saves (or, in --smoke, prints the timing line);
    // the process exit code.
    int finish();

private:
    enum class Screen : uint8_t { None, Inventory, Kiln, Crate };

    // One slot on an open screen, rebuilt every frame for drawing and clicks.
    struct SlotRef {
        ItemStack* stack;
        SlotKind kind;
        Rectangle rect;
        int group;  // 0 inventory 9..35, 1 hotbar, 2 armor, 3 container
    };

    void handle_input(float dt);
    void update_survival(const PlayerInput& in, Vector3 before, bool was_on_ground, float dt);
    void update_eating(float dt);
    void die();
    void start_respawn();
    void update_respawn();
    void toast(const char* text);
    void update_mining(const RayHit& hit, float dt);
    void use_block(const RayHit& hit);
    void break_block(BlockPos p, bool survival_drops);
    void drop_stack(ItemStack s, Vector3 from);
    void open_screen(Screen s, BlockPos at = BlockPos{0, 0, 0});
    void close_screen();
    void tick_kilns(float dt);
    std::vector<SlotRef> screen_slots();
    void screen_click(const std::vector<SlotRef>& slots);
    void shift_move(const SlotRef& from);
    void draw(const RayHit& hit);
    void draw_screen(const std::vector<SlotRef>& slots);
    void smoke_step(float dt);
    UiTextures textures() const;

    CliOptions opts_;
    std::filesystem::path meta_file_;  // empty in --smoke, which writes nothing
    std::unique_ptr<RegionStore> store_;
    std::optional<WorldMeta> meta_;
    World world_;
    ThreadPool pool_;
    ChunkManager mgr_;
    Renderer renderer_;
    Player player_{};
    WorldTime time_;
    ColumnPos spawn_col_{0, 0};
    GameMode mode_ = GameMode::Survival;
    Inventory inv_;
    ItemEntities items_;

    // Survival.
    Vitals vitals_;
    DamageState damage_;
    FallTracker fall_;
    EatState eating_;
    int mined_since_tick_ = 0;
    std::optional<BlockPos> bedroll_;  // the respawn point set by using a bedroll
    bool dead_ = false;                // the death screen is up
    bool respawning_ = false;          // waiting for the respawn columns to load
    bool respawn_to_origin_ = false;   // no usable bedroll: respawn at spawn_player's position
    ColumnPos respawn_col_{0, 0};      // streamed while respawning_
    std::string death_note_;           // where the last death's items went ("" when unknown)
    float hurt_flash_ = 0.f;           // seconds left of the red flash
    float last_health_ = MAX_HEALTH;
    std::string toast_;
    float toast_left_ = 0.f;

    // Screens.
    Screen screen_ = Screen::None;
    BlockPos open_pos_{0, 0, 0};
    ItemStack cursor_{};
    int scroll_ = 0;
    bool screen_fresh_ = false;  // opened this frame: the opening click must not also hit a slot

    // Mining.
    bool mining_ = false;
    BlockPos mine_pos_{0, 0, 0};
    float mine_progress_ = 0.f, break_cooldown_ = 0.f;
    uint32_t drop_rng_ = 1;
    DoubleTap space_tap_;

    bool window_open_ = false;
    bool captured_ = false, swallow_click_ = false, spawned_ = false, debug_ = false;
    int drawn_ = 0, smoke_frames_ = 0;
    float smoke_max_frame_ = 0.f;
    Vector3 smoke_anchor_{};
    double start_ = 0;
};
