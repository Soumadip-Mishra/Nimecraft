// One running game: world load and save, the per-frame loop, input, mining and placing, item
// entities, kilns, the inventory, crafting, kiln, and crate screens, Creative mode, survival
// (vitals, eating, fall damage, death, grave cairns, bedroll respawn, sleeping), creatures
// (AI, spawning, despawning, melee, thorns, loot), block ticks (flowing water, falling sand and
// gravel, plant growth, planting), and the HUD.
// Device module (window, input, clock): decisions live in the tested logic modules; this class only
// wires them to the window. Covered by --smoke and the Manual checks.
#pragma once

#include "audio.hpp"
#include "chunk_manager.hpp"
#include "cli.hpp"
#include "combat.hpp"
#include "inventory.hpp"
#include "item_entities.hpp"
#include "jobs.hpp"
#include "mobs.hpp"
#include "player.hpp"
#include "raycast.hpp"
#include "render.hpp"
#include "settings.hpp"
#include "storage.hpp"
#include "ticks.hpp"
#include "ui_draw.hpp"
#include "ui_logic.hpp"
#include "vitals.hpp"
#include "world.hpp"
#include "world_time.hpp"

#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Game {
public:
    // Loads world.meta and starts streaming; the window (and audio) must already be open (App in
    // screens.*). o.world is the folder under saves/; o.rd the render distance; fov and mouse
    // sensitivity come from s.
    Game(const CliOptions& o, Audio& audio, const Settings& s);
    ~Game();  // frees the GPU resources if finish() was not called
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // One frame between the caller's BeginDrawing and EndDrawing: input and world updates (unless
    // paused, which only draws the frozen world), then the world, HUD, and panels. False once
    // --smoke has finished (or timed out waiting for the spawn column).
    bool frame(bool paused);
    bool save();  // modified columns, regions, and world.meta; false if a write failed
    // Closes any open panel and frees the GPU resources, then saves (or, in --smoke, prints the
    // timing line); the process exit code.
    int finish();

    bool spawned() const { return spawned_; }
    bool dead() const { return dead_; }
    int smoke_frames() const { return smoke_frames_; }
    // Esc pressed while playing (no panel open, alive): the caller shows the pause menu. Reading clears it.
    bool take_pause_request();
    // "Quit to Title" clicked on the death screen. Reading clears it.
    bool take_quit_request();
    // Paused: the cursor is free. Resumed: the cursor is captured again (the resuming click does
    // nothing in the world).
    void set_paused(bool paused);
    // A changed settings screen: render distance, field of view, and mouse sensitivity.
    void apply_settings(const Settings& s);

private:
    enum class Panel : uint8_t { None, Inventory, Kiln, Crate };

    // One slot on an open screen, rebuilt every frame for drawing and clicks.
    struct SlotRef {
        ItemStack* stack;
        SlotKind kind;
        Rectangle rect;
        int group;  // 0 inventory 9..35, 1 hotbar, 2 armor, 3 container
    };

    void handle_input(float dt);
    void update_survival(const PlayerInput& in, Vector3 before, bool was_on_ground, float dt);
    void note_health();
    void update_eating(float dt);
    void update_mobs(float dt, bool alive, std::optional<uint32_t> hit_mob);
    std::optional<uint32_t> attack_mob(const RayHit& block_hit);
    void apply_mob_events(bool alive);
    void die();
    void start_respawn();
    void update_respawn();
    void toast(const char* text);
    void update_mining(const RayHit& hit, float dt);
    void use_block(const RayHit& hit);
    void break_block(BlockPos p, bool survival_drops);
    void drop_stack(ItemStack s, Vector3 from);
    void open_screen(Panel s, BlockPos at = BlockPos{0, 0, 0});
    void close_screen();
    void tick_kilns(float dt);
    void update_block_ticks(float dt);
    std::vector<SlotRef> screen_slots();
    void screen_click(const std::vector<SlotRef>& slots);
    void shift_move(const SlotRef& from);
    RayHit update(float dt);
    void stage_save();
    void autosave();
    void draw(const RayHit& hit, bool paused);
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

    // Sound (the App's device) and the view settings.
    Audio& audio_;
    float fov_ = 70.f, sensitivity_ = 1.f;
    bool pause_request_ = false, quit_request_ = false, gpu_ready_ = false;

    // Autosave every AUTOSAVE_S of play; the region files are written by a worker.
    float autosave_clock_ = 0.f;
    std::future<bool> autosave_;
    Footsteps steps_;
    float dig_clock_ = 0.f;          // seconds until the next Dig sound while mining
    bool was_wet_ = false;           // feet in water last frame (entering plays Splash)
    uint32_t ambient_rng_ = 0x51ED270Bu;  // creature calls

    // Creatures.
    MobManager mobs_;
    std::vector<Projectile> thorns_;
    float spawn_clock_ = 0.f;          // spawn_tick runs once per second
    uint32_t spawn_rng_ = 0x2545F491u;
    uint32_t loot_rng_ = 0x7F4A7C15u;
    std::optional<uint32_t> target_mob_;  // the creature under the crosshair within reach
    int attacks_since_tick_ = 0;

    // Block ticks (20 per second): scheduled ticks and random ticks.
    TickScheduler ticks_;
    TickClock tick_clock_;
    uint32_t random_tick_rng_ = 0x1B873593u;
    std::vector<BlockPos> random_tick_buf_;

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

    // Panels (inventory, kiln, crate).
    Panel screen_ = Panel::None;
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

    bool captured_ = false, swallow_click_ = false, spawned_ = false, debug_ = false;
    int drawn_ = 0, smoke_frames_ = 0;
    float smoke_max_frame_ = 0.f;
    Vector3 smoke_anchor_{};
    double start_ = 0;
};
