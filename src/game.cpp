#include "game.hpp"

#include "block.hpp"
#include "block_entities.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "death.hpp"
#include "mining.hpp"
#include "platform.hpp"
#include "recipes.hpp"

#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace {

constexpr double DEBUG_TIME_STEP = 1000;  // F4 skips this many ticks
constexpr Color UNDERWATER{20, 60, 160, 110};  // screen tint while the eye is in WATER
constexpr int SMOKE_FRAMES = 600;
constexpr int SMOKE_UI_FROM = 400, SMOKE_UI_TO = 430;  // --smoke shows the inventory screen
constexpr double SMOKE_SPAWN_TIMEOUT_S = 60.0;
constexpr std::size_t MESH_UPLOADS_PER_FRAME = 16;
constexpr float BREAK_COOLDOWN = 0.2f;  // seconds between two broken blocks while LMB is held
constexpr float ITEM_DRAW_SIZE = 0.3f;
constexpr float RECIPE_ROW = 30.f, SIDE_W = 330.f;
constexpr int RECIPE_ROWS = 10, PALETTE_COLS = 7, PALETTE_ROWS = 7;
constexpr float SCREEN_REACH = 8.f;  // a kiln or crate screen closes beyond this distance
constexpr float HURT_FLASH_S = 0.35f, TOAST_S = 3.f;
constexpr int SMOKE_DEATH_FROM = 440, SMOKE_DEATH_TO = 460;  // --smoke draws the death screen
const ColumnPos ORIGIN_COLUMN{0, 0};                        // spawn_player's column

Vector3 on_top_of(BlockPos b) {
    return Vector3{static_cast<float>(b.x) + 0.5f, static_cast<float>(b.y + 1), static_cast<float>(b.z) + 0.5f};
}

ColumnPos column_at(Vector3 pos) {
    return column_of(chunk_of(static_cast<int>(std::floor(pos.x)), 0,
                              static_cast<int>(std::floor(pos.z))));
}

bool column_modified(const World& w, ColumnPos c) {
    for (int y = 0; y < SECTIONS; ++y) {
        const Chunk* ch = w.chunk(ChunkPos{c.x, y, c.z});
        if (ch != nullptr && ch->modified) return true;
    }
    return false;
}

Column copy_column(const World& w, ColumnPos c) {
    Column col;
    for (int y = 0; y < SECTIONS; ++y)
        if (const Chunk* ch = w.chunk(ChunkPos{c.x, y, c.z}))
            col[static_cast<std::size_t>(y)] = std::make_unique<Chunk>(*ch);
    return col;
}

std::filesystem::path world_dir(const std::string& name) {
    // Command-line text is UTF-8 on Linux; build the path from UTF-8 so non-ASCII names work.
    const std::u8string utf8(reinterpret_cast<const char8_t*>(name.data()), name.size());
    return user_data_dir() / "saves" / std::filesystem::path(utf8);
}

std::filesystem::path meta_path(const CliOptions& o) {
    return o.smoke ? std::filesystem::path{} : world_dir(o.world) / "world.meta";
}

uint32_t pick_seed(const CliOptions& o, const std::optional<WorldMeta>& meta) {
    if (meta) return meta->seed;
    return o.seed.value_or(o.smoke ? 1u : static_cast<uint32_t>(std::time(nullptr)));
}

unsigned worker_count() {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 1 ? hw - 1 : 1;
}

PlayerInput read_input(bool captured) {
    PlayerInput in;
    if (!captured) return in;
    const Vector2 d = GetMouseDelta();
    in.mouse_dx = d.x;
    in.mouse_dy = d.y;
    in.forward = static_cast<float>(IsKeyDown(KEY_W)) - static_cast<float>(IsKeyDown(KEY_S));
    in.right = static_cast<float>(IsKeyDown(KEY_D)) - static_cast<float>(IsKeyDown(KEY_A));
    in.jump = IsKeyDown(KEY_SPACE);
    in.descend = IsKeyDown(KEY_LEFT_SHIFT);
    in.sprint = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && in.forward > 0.f;
    return in;
}

int update_hotbar(int selected) {
    for (int i = 0; i < 9; ++i)
        if (IsKeyPressed(KEY_ONE + i)) selected = i;
    const float wheel = GetMouseWheelMove();
    if (wheel > 0) selected = (selected + 8) % 9;
    if (wheel < 0) selected = (selected + 1) % 9;
    return selected;
}

void draw_centered_text(const char* text, int y, int size, Color c) {
    DrawText(text, (GetScreenWidth() - MeasureText(text, size)) / 2, y, size, c);
}

void draw_crosshair() {
    const int cx = GetScreenWidth() / 2, cy = GetScreenHeight() / 2;
    DrawRectangle(cx - 9, cy - 1, 18, 2, WHITE);
    DrawRectangle(cx - 1, cy - 9, 2, 18, WHITE);
}

void draw_debug(const Player& p, const World& w, const ChunkManager& mgr, int drawn, const WorldTime& t,
                std::size_t items) {
    constexpr int LINES = 7;
    char lines[LINES][96];
    std::snprintf(lines[0], sizeof lines[0], "FPS %d", GetFPS());
    std::snprintf(lines[1], sizeof lines[1], "XYZ %.2f %.2f %.2f", static_cast<double>(p.pos.x),
                  static_cast<double>(p.pos.y), static_cast<double>(p.pos.z));
    std::snprintf(lines[2], sizeof lines[2], "Loaded columns %zu", w.loaded_columns().size());
    std::snprintf(lines[3], sizeof lines[3], "Pending jobs %zu", mgr.pending_jobs());
    std::snprintf(lines[4], sizeof lines[4], "Chunks drawn %d", drawn);
    std::snprintf(lines[5], sizeof lines[5], "Time %d %s (F4 +1000)", t.day_tick(), t.is_night() ? "night" : "day");
    std::snprintf(lines[6], sizeof lines[6], "Dropped items %zu", items);
    DrawRectangle(6, 6, 300, LINES * 20 + 8, Color{0, 0, 0, 140});
    for (int i = 0; i < LINES; ++i) DrawText(lines[i], 12, 12 + i * 20, 18, WHITE);
}

// Brightness of the cell holding pos, as the chunk shader lights blocks (sky * daylight vs block).
Color light_tint(const World& w, Vector3 pos, float day_light) {
    const int x = static_cast<int>(std::floor(pos.x)), y = static_cast<int>(std::floor(pos.y)),
              z = static_cast<int>(std::floor(pos.z));
    const Chunk* c = (y >= 0 && y < WORLD_HEIGHT) ? w.chunk(chunk_of(x, y, z)) : nullptr;
    float l = 1.f;
    if (c != nullptr) {
        const int i = Chunk::index(floor_mod(x, CHUNK), floor_mod(y, CHUNK), floor_mod(z, CHUNK));
        l = std::max(static_cast<float>(sky_light(*c, i)) * day_light, static_cast<float>(block_light(*c, i))) / 15.f;
    }
    const auto v = static_cast<unsigned char>(std::clamp(40.f + 215.f * l, 0.f, 255.f));
    return Color{v, v, v, 255};
}

bool is_smelt_input(ItemId id) {
    for (const SmeltRecipe& r : smelt_recipes())
        for (const ItemStack& in : r.inputs)
            if (!in.empty() && in.id == id) return true;
    return false;
}

std::string recipe_text(const Recipe& r) {
    std::string s;
    for (std::size_t i = 0; i < r.inputs.size(); ++i) {
        if (i > 0) s += " + ";
        s += std::to_string(r.inputs[i].count) + " " + std::string(item_def(r.inputs[i].id).name);
    }
    return s;
}

}  // namespace

Game::Game(const CliOptions& o)
    : opts_(o),
      meta_file_(meta_path(o)),
      // --smoke plays a throwaway world: no save folder, no store, nothing written.
      store_(o.smoke ? nullptr : std::make_unique<RegionStore>(meta_file_.parent_path())),
      meta_(o.smoke ? std::nullopt : load_meta(meta_file_)),
      world_(pick_seed(o, meta_)),
      pool_(worker_count()),
      mgr_(world_, pool_, store_.get(), o.rd) {
    mode_ = o.creative ? GameMode::Creative : GameMode::Survival;
    if (meta_) {
        time_.ticks = meta_->time;
        player_.pos = Vector3{meta_->px, meta_->py, meta_->pz};
        player_.yaw = meta_->yaw;
        player_.pitch = meta_->pitch;
        spawn_col_ = column_at(player_.pos);
        mode_ = meta_->mode;  // a saved world keeps its mode
        inv_ = meta_->inv;
        vitals_ = meta_->vitals;
        bedroll_ = meta_->spawn;
        last_health_ = vitals_.health;
        dead_ = mode_ == GameMode::Survival && vitals_.health <= 0.f;  // quit on the death screen
    }

    // --smoke paces itself at 60 FPS instead of vsync: a compositor throttles a vsynced window it
    // considers hidden to about 1 frame per second, which would measure the desktop, not the game.
    // It also keeps running if the compositor iconifies the window; raylib otherwise blocks in
    // EndDrawing until the next window event, and the smoke run never finishes.
    const unsigned pacing = o.smoke ? static_cast<unsigned>(FLAG_WINDOW_ALWAYS_RUN)
                                    : static_cast<unsigned>(FLAG_VSYNC_HINT);
    SetConfigFlags(pacing | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Nimecraft");
    window_open_ = true;
    if (o.smoke) SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    renderer_.init();

    captured_ = !o.smoke && !dead_;
    if (captured_) DisableCursor();
    start_ = GetTime();
}

Game::~Game() {
    if (!window_open_) return;
    renderer_.shutdown();
    CloseWindow();
}

UiTextures Game::textures() const {
    return UiTextures{renderer_.block_atlas(), renderer_.item_atlas()};
}

void Game::handle_input(float dt) {
    swallow_click_ = false;
    toast_left_ = std::max(0.f, toast_left_ - dt);
    hurt_flash_ = std::max(0.f, hurt_flash_ - dt);
    if (dead_ || respawning_) {  // no play input; the world keeps its time
        if (IsKeyPressed(KEY_F3)) debug_ = !debug_;
        if (dead_ && spawned_ && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(GetMousePosition(), death_button_rect()))
            start_respawn();
        if (spawned_) time_.advance(dt);
        return;
    }
    if (!opts_.smoke) {
        if (screen_ != Screen::None) {
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_E)) close_screen();
        } else if (captured_ && IsKeyPressed(KEY_ESCAPE)) {
            // Esc releases the cursor; the click that recaptures it does nothing else.
            EnableCursor();
            captured_ = false;
        } else if (!captured_ && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                                  IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            DisableCursor();
            captured_ = true;
            swallow_click_ = true;
        } else if (spawned_ && captured_ && IsKeyPressed(KEY_E)) {
            open_screen(Screen::Inventory);
        }
    }
    if (IsKeyPressed(KEY_F3)) debug_ = !debug_;
    if (IsKeyPressed(KEY_F4)) time_.ticks += DEBUG_TIME_STEP;

    const bool playing = screen_ == Screen::None;
    if (playing) inv_.selected = update_hotbar(inv_.selected);
    if (playing && captured_ && spawned_ && IsKeyPressed(KEY_Q) && !inv_.held().empty()) {
        ItemStack& held = inv_.held();
        drop_stack(ItemStack{held.id, 1, held.damage}, player_eye(player_));
        if (mode_ == GameMode::Survival && --held.count == 0) held = ItemStack{};
    }
    if (mode_ == GameMode::Creative && playing && captured_ && IsKeyPressed(KEY_SPACE) && space_tap_.press(GetTime()))
        player_.flying = !player_.flying;
    if (mode_ != GameMode::Creative) player_.flying = false;

    PlayerInput in = read_input(captured_ && !swallow_click_ && playing);
    if (!playing) {  // keep the look and the walk still while a screen is open
        in = PlayerInput{};
    }
    if (opts_.smoke && playing) {
        in.forward = 1.f;
        in.jump = true;  // hop up 1-block steps so the walk keeps streaming new terrain
    }
    // Sprinting needs food, and does nothing in water (so it costs nothing there either).
    if ((mode_ == GameMode::Survival && !can_sprint(vitals_)) || (spawned_ && in_water(player_, world_)))
        in.sprint = false;
    if (spawned_) {
        const Vector3 before = player_.pos;
        const bool was_on_ground = player_.on_ground;
        player_update(player_, world_, in, dt);
        time_.advance(dt);
        update_survival(in, before, was_on_ground, dt);
    }
}

void Game::update_survival(const PlayerInput& in, Vector3 before, bool was_on_ground, float dt) {
    const bool survival = mode_ == GameMode::Survival;
    VitalsInput vin;
    if (!player_.flying) {
        const float dx = player_.pos.x - before.x, dz = player_.pos.z - before.z;
        const float moved = std::sqrt(dx * dx + dz * dz);
        (in.sprint ? vin.sprinted_m : vin.walked_m) = moved;
    }
    vin.jumps = (in.jump && was_on_ground && !player_.on_ground && player_.vel.y > 0.f) ? 1 : 0;
    vin.blocks_mined = mined_since_tick_;
    vin.eye_in_water = eye_in_water(player_, world_);
    mined_since_tick_ = 0;
    vitals_tick(vitals_, vin, dt, mode_);
    damage_tick(damage_, dt);

    const bool wet = in_water(player_, world_);
    const float fell = track_fall(fall_, player_.pos.y, player_.on_ground, wet || player_.flying || !survival);
    if (survival && fell > 0.f) take_damage(vitals_, damage_, fall_damage(fell, wet), armor_points(inv_));

    if (vitals_.health < last_health_) hurt_flash_ = HURT_FLASH_S;
    last_health_ = vitals_.health;
    if (survival && vitals_.health <= 0.f) die();
}

void Game::die() {
    close_screen();  // a stack on the cursor goes back into the inventory first
    dead_ = true;
    mining_ = false;
    mine_progress_ = 0.f;
    eating_ = EatState{};
    player_.vel = Vector3{};
    const bool had_items = std::any_of(inv_.slots.begin(), inv_.slots.end(), [](const ItemStack& s) { return !s.empty(); }) ||
                           std::any_of(inv_.armor.begin(), inv_.armor.end(), [](const ItemStack& s) { return !s.empty(); });
    death_note_.clear();
    if (const std::optional<BlockPos> spot = find_cairn_spot(world_, player_.pos); spot && had_items) {
        // A plant or torch in the cell is replaced by the cairn: it drops like a broken block.
        const BlockId replaced = world_.get(spot->x, spot->y, spot->z);
        const Vector3 at{spot->x + 0.5f, spot->y + 0.25f, spot->z + 0.5f};
        if (replaced != blk::AIR)
            if (const std::optional<ItemStack> d = block_drop(replaced, ItemStack{}, drop_rng_++ * 0x9E3779B9u))
                items_.spawn(at, *d);
        place_cairn(world_, *spot, inv_);
        if (world_.get(spot->x, spot->y, spot->z) == blk::GRAVE_CAIRN)
            death_note_ = "Your items wait in a grave cairn where you fell.";
        mgr_.flush_edits();
    }
    if (had_items && death_note_.empty()) death_note_ = "No room for a cairn: your items fell to the ground.";
    // No spot (or a cell that could not be set): everything drops at the feet instead.
    for (ItemStack& s : inv_.slots)
        if (!s.empty()) items_.spawn(player_.pos, std::exchange(s, ItemStack{}));
    for (ItemStack& s : inv_.armor)
        if (!s.empty()) items_.spawn(player_.pos, std::exchange(s, ItemStack{}));
    if (!opts_.smoke) {
        EnableCursor();
        captured_ = false;
    }
}

// The player stays dead where they fell (health 0, so a save made now reloads on the death screen)
// until the respawn column has loaded; only streaming moves to it.
void Game::start_respawn() {
    dead_ = false;
    respawning_ = true;
    respawn_to_origin_ = !bedroll_;
    respawn_col_ = bedroll_ ? column_at(on_top_of(*bedroll_)) : ORIGIN_COLUMN;
    if (!opts_.smoke) {
        DisableCursor();
        captured_ = true;
    }
}

void Game::update_respawn() {
    if (!respawning_ || !mgr_.column_ready(respawn_col_)) return;
    if (!respawn_to_origin_ && !bedroll_usable(world_, *bedroll_)) {
        respawn_to_origin_ = true;
        respawn_col_ = ORIGIN_COLUMN;
        toast("Your bedroll is missing or blocked");
        return;
    }
    player_.pos = respawn_point(world_, respawn_to_origin_ ? std::nullopt : bedroll_);
    player_.vel = Vector3{};
    player_.flying = false;
    vitals_ = Vitals{};
    last_health_ = vitals_.health;
    damage_ = DamageState{};
    hurt_flash_ = 0.f;
    fall_ = FallTracker{};
    respawning_ = false;
}

void Game::toast(const char* text) {
    toast_ = text;
    toast_left_ = TOAST_S;
}

void Game::update_eating(float dt) {
    ItemStack& held = inv_.held();
    const bool use = mode_ == GameMode::Survival && spawned_ && captured_ && !swallow_click_ &&
                     screen_ == Screen::None && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !held.empty();
    if (!eat_tick(eating_, use, held.id, vitals_, dt)) return;
    eat(vitals_, item_def(held.id));
    if (--held.count == 0) held = ItemStack{};
}

void Game::drop_stack(ItemStack s, Vector3 from) {
    if (s.empty()) return;
    items_.spawn(Vector3Add(from, Vector3Scale(player_look(player_), 0.6f)), s);
    // Throw it forward: the spawn pop plus the look direction.
    ItemEntity& e = items_.all().back();
    const Vector3 look = player_look(player_);
    e.vel = Vector3Add(e.vel, Vector3Scale(look, 4.f));
    e.age = 0.f;
}

void Game::break_block(BlockPos p, bool survival_drops) {
    const BlockId b = world_.get(p.x, p.y, p.z);
    if (!block_def(b).breakable || b == blk::AIR) return;
    const Vector3 center{p.x + 0.5f, p.y + 0.25f, p.z + 0.5f};
    const GameMode mode = survival_drops ? GameMode::Survival : GameMode::Creative;
    for (const ItemStack& s : break_drops(b, world_.block_entity(p), inv_.held(), mode, drop_rng_++ * 0x9E3779B9u))
        items_.spawn(center, s);
    if (world_.set(p.x, p.y, p.z, blk::AIR)) mgr_.flush_edits();
    if (screen_ != Screen::None && screen_ != Screen::Inventory && p == open_pos_) close_screen();
    mining_ = false;
    mine_progress_ = 0.f;
}

void Game::update_mining(const RayHit& hit, float dt) {
    break_cooldown_ = std::max(0.f, break_cooldown_ - dt);
    const bool held = spawned_ && captured_ && !swallow_click_ && screen_ == Screen::None && hit.hit &&
                      IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (!held) {
        mining_ = false;
        mine_progress_ = 0.f;
        return;
    }
    const BlockPos p{hit.x, hit.y, hit.z};
    if (!mining_ || !(p == mine_pos_)) {  // a new target starts over
        mining_ = true;
        mine_pos_ = p;
        mine_progress_ = 0.f;
    }
    if (mode_ == GameMode::Creative) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) break_block(p, false);  // instant, no drops
        return;
    }
    if (break_cooldown_ > 0.f) return;
    mine_progress_ += dt;
    if (mine_progress_ >= break_time(world_.get(p.x, p.y, p.z), inv_.held())) {
        break_block(p, true);
        ++mined_since_tick_;
        break_cooldown_ = BREAK_COOLDOWN;
    }
}

void Game::use_block(const RayHit& hit) {
    if (!spawned_ || !captured_ || swallow_click_ || screen_ != Screen::None || !hit.hit) return;
    if (!IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) return;
    const BlockPos target{hit.x, hit.y, hit.z};
    const BlockId b = world_.get(hit.x, hit.y, hit.z);
    if (!IsKeyDown(KEY_LEFT_SHIFT)) {
        if (b == blk::KILN || b == blk::CRATE) {
            if (world_.block_entity(target) == nullptr)
                world_.set_block_entity(target, b == blk::KILN ? BlockEntity{KilnState{}} : BlockEntity{CrateState{}});
            open_screen(b == blk::KILN ? Screen::Kiln : Screen::Crate, target);
            return;
        }
        if (b == blk::WORKBENCH) {
            open_screen(Screen::Inventory);
            return;
        }
        if (b == blk::BEDROLL) {  // sets the respawn point; at night, also sleeps until dawn
            bedroll_ = target;
            if (can_sleep(time_, false)) {  // M6 supplies whether hostiles are within 8 blocks
                sleep_until_dawn(time_);
                toast("You slept until dawn - respawn point set");
            } else {
                toast("Respawn point set - you can sleep only at night");
            }
            return;
        }
    }
    ItemStack& held = inv_.held();
    if (held.empty() || held.id >= blk::COUNT || held.id == blk::WATER) return;  // only blocks are placed
    const int x = hit.x + hit.nx, y = hit.y + hit.ny, z = hit.z + hit.nz;
    const bool has_face = hit.nx != 0 || hit.ny != 0 || hit.nz != 0;
    if (!has_face || world_.get(x, y, z) != blk::AIR || player_intersects_block(player_, x, y, z)) return;
    const BlockId placed = held.id;
    if (!world_.set(x, y, z, placed)) return;
    if (placed == blk::KILN) world_.set_block_entity(BlockPos{x, y, z}, KilnState{});
    if (placed == blk::CRATE) world_.set_block_entity(BlockPos{x, y, z}, CrateState{});
    mgr_.flush_edits();
    if (mode_ == GameMode::Survival && --held.count == 0) held = ItemStack{};
}

void Game::open_screen(Screen s, BlockPos at) {
    screen_ = s;
    open_pos_ = at;
    scroll_ = 0;
    screen_fresh_ = true;
    if (!opts_.smoke) EnableCursor();
}

void Game::close_screen() {
    if (screen_ == Screen::None) return;
    // A stack still on the cursor goes back into the inventory, or drops if it does not fit.
    if (!cursor_.empty()) {
        const int left = inv_.add(cursor_);
        if (left > 0) drop_stack(ItemStack{cursor_.id, static_cast<uint8_t>(left), cursor_.damage}, player_eye(player_));
        cursor_ = ItemStack{};
    }
    screen_ = Screen::None;
    if (!opts_.smoke && captured_) DisableCursor();
}

void Game::tick_kilns(float dt) {
    for (const BlockPos& p : world_.block_entity_positions())
        if (BlockEntity* be = world_.block_entity(p))
            if (auto* k = std::get_if<KilnState>(be)) kiln_tick(*k, dt);
}

std::vector<Game::SlotRef> Game::screen_slots() {
    std::vector<SlotRef> out;
    if (screen_ == Screen::None) return out;
    const float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    const float grid_w = 9 * UI_SLOT + 8 * UI_GAP;
    const float x0 = std::floor((w - grid_w) / 2) - (screen_ == Screen::Inventory ? SIDE_W / 2 : 0.f);
    const float inv_y = std::floor(h / 2) + 10;
    const Vector2 main{x0, inv_y}, hotbar{x0, inv_y + 3 * (UI_SLOT + UI_GAP) + 8};
    for (int i = 0; i < 27; ++i)
        out.push_back(SlotRef{&inv_.slots[static_cast<std::size_t>(9 + i)], SlotKind::Any,
                              grid_slot_rect(main, i, 9, UI_SLOT, UI_GAP), 0});
    for (int i = 0; i < 9; ++i)
        out.push_back(SlotRef{&inv_.slots[static_cast<std::size_t>(i)], SlotKind::Any,
                              grid_slot_rect(hotbar, i, 9, UI_SLOT, UI_GAP), 1});
    const float top = inv_y - 4 * (UI_SLOT + UI_GAP) - 24;
    if (screen_ == Screen::Inventory) {
        constexpr SlotKind ARMOR[4] = {SlotKind::Head, SlotKind::Chest, SlotKind::Legs, SlotKind::Feet};
        for (int i = 0; i < 4; ++i)
            out.push_back(SlotRef{&inv_.armor[static_cast<std::size_t>(i)], ARMOR[i],
                                  grid_slot_rect(Vector2{x0, top + 20}, i, 1, UI_SLOT, UI_GAP), 2});
        return out;
    }
    BlockEntity* be = world_.block_entity(open_pos_);
    if (be == nullptr) return out;
    if (auto* crate = std::get_if<CrateState>(be)) {
        for (int i = 0; i < 27; ++i)
            out.push_back(SlotRef{&crate->slots[static_cast<std::size_t>(i)], SlotKind::Any,
                                  grid_slot_rect(Vector2{x0, top + 20}, i, 9, UI_SLOT, UI_GAP), 3});
    } else if (auto* kiln = std::get_if<KilnState>(be)) {
        const float kx = x0 + 2 * (UI_SLOT + UI_GAP), ky = top + 20;
        out.push_back(SlotRef{&kiln->in[0], SlotKind::Any, Rectangle{kx, ky, UI_SLOT, UI_SLOT}, 3});
        out.push_back(SlotRef{&kiln->in[1], SlotKind::Any, Rectangle{kx + UI_SLOT + UI_GAP, ky, UI_SLOT, UI_SLOT}, 3});
        out.push_back(SlotRef{&kiln->fuel, SlotKind::Any,
                              Rectangle{kx + (UI_SLOT + UI_GAP) / 2, ky + 2 * (UI_SLOT + UI_GAP), UI_SLOT, UI_SLOT}, 3});
        out.push_back(SlotRef{&kiln->out, SlotKind::TakeOnly,
                              Rectangle{kx + 4 * (UI_SLOT + UI_GAP), ky + UI_SLOT + UI_GAP, UI_SLOT, UI_SLOT}, 3});
    }
    return out;
}

void Game::shift_move(const SlotRef& from) {
    ItemStack& s = *from.stack;
    if (s.empty()) return;
    std::span<ItemStack> all(inv_.slots);
    if (from.group == 3) {  // container -> inventory, hotbar first
        quick_move(s, all);
        return;
    }
    if (from.group == 2) {  // armor -> inventory
        quick_move(s, all.subspan(9));
        return;
    }
    if (screen_ == Screen::Crate) {
        if (auto* crate = std::get_if<CrateState>(world_.block_entity(open_pos_))) quick_move(s, crate->slots);
        return;
    }
    if (screen_ == Screen::Kiln) {
        if (auto* kiln = std::get_if<KilnState>(world_.block_entity(open_pos_))) {
            if (is_smelt_input(s.id)) quick_move(s, kiln->in);
            else if (fuel_seconds(s.id) > 0.f) quick_move(s, std::span<ItemStack>(&kiln->fuel, 1));
        }
        return;
    }
    // Inventory screen: armor goes on, everything else moves between hotbar and inventory.
    const ArmorSlot a = item_def(s.id).armor;
    if (a != ArmorSlot::None) {
        ItemStack& slot = inv_.armor[static_cast<std::size_t>(a) - 1];
        if (slot.empty()) {
            slot = s;
            s = ItemStack{};
            return;
        }
    }
    quick_move(s, from.group == 1 ? all.subspan(9) : all.subspan(0, 9));
}

void Game::screen_click(const std::vector<SlotRef>& slots) {
    const bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT), right = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.f) scroll_ -= wheel > 0 ? 1 : -1;
    if (!left && !right) return;
    const Vector2 m = GetMousePosition();
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    for (const SlotRef& s : slots) {
        if (!CheckCollisionPointRec(m, s.rect)) continue;
        if (shift && left) shift_move(s);
        else click_slot(cursor_, *s.stack, s.kind, left ? Click::Left : Click::Right);
        return;
    }
    if (screen_ != Screen::Inventory) return;
    // The side list: recipes (Survival) or the item palette (Creative).
    const float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    const float grid_w = 9 * UI_SLOT + 8 * UI_GAP;
    const float side_x = std::floor((w - grid_w) / 2) - SIDE_W / 2 + grid_w + 20;
    const float side_y = std::floor(h / 2) + 10 - 4 * (UI_SLOT + UI_GAP) - 4;
    if (mode_ == GameMode::Creative) {
        const std::vector<ItemId> palette = creative_palette();
        const int rows = (static_cast<int>(palette.size()) + PALETTE_COLS - 1) / PALETTE_COLS;
        scroll_ = clamp_scroll(scroll_, rows, PALETTE_ROWS);
        const int i = grid_slot_at(m, Vector2{side_x, side_y}, PALETTE_COLS, PALETTE_ROWS, UI_SLOT, UI_GAP);
        if (i < 0) return;
        const std::size_t at = static_cast<std::size_t>(scroll_ * PALETTE_COLS + i);
        if (!cursor_.empty()) {
            cursor_ = ItemStack{};  // clicking the palette with a stack throws it away
        } else if (at < palette.size()) {
            const ItemId id = palette[at];
            cursor_ = ItemStack{id, static_cast<uint8_t>(left ? item_def(id).max_stack : 1), 0};
        }
        return;
    }
    const StationSet near = stations_near(world_, Vector3{player_.pos.x, player_.pos.y + PLAYER_H / 2, player_.pos.z});
    const std::vector<const Recipe*> list = available(inv_, near);
    scroll_ = clamp_scroll(scroll_, static_cast<int>(list.size()), RECIPE_ROWS);
    const float row = std::floor((m.y - side_y) / RECIPE_ROW);
    if (m.x < side_x || m.x > side_x + SIDE_W - 20 || row < 0 || row >= RECIPE_ROWS) return;
    const std::size_t at = static_cast<std::size_t>(scroll_ + static_cast<int>(row));
    if (at >= list.size()) return;
    const int times = shift ? 64 : 1;  // shift-click crafts as many as fit, up to 64 times
    for (int i = 0; i < times && craft(inv_, *list[at]); ++i) {
    }
}

void Game::draw_screen(const std::vector<SlotRef>& slots) {
    const UiTextures t = textures();
    const float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 110});
    const float grid_w = 9 * UI_SLOT + 8 * UI_GAP;
    const float x0 = std::floor((w - grid_w) / 2) - (screen_ == Screen::Inventory ? SIDE_W / 2 : 0.f);
    const float inv_y = std::floor(h / 2) + 10;
    const float top = inv_y - 4 * (UI_SLOT + UI_GAP) - 24;
    const float panel_w = grid_w + 24 + (screen_ == Screen::Inventory ? SIDE_W : 0.f);
    draw_panel(Rectangle{x0 - 12, top - 12, panel_w, inv_y + 4 * (UI_SLOT + UI_GAP) + 20 - (top - 12)});
    const char* title = screen_ == Screen::Kiln ? "Kiln" : (screen_ == Screen::Crate ? "Crate" : "Inventory");
    draw_label(title, x0, top - 6, 20, WHITE);

    const Vector2 m = GetMousePosition();
    const ItemStack* hovered = nullptr;
    for (const SlotRef& s : slots) {
        const bool over = CheckCollisionPointRec(m, s.rect);
        if (over) hovered = s.stack;
        draw_slot(t, *s.stack, s.rect, false, over);
    }
    if (screen_ == Screen::Kiln)
        if (const auto* kiln = std::get_if<KilnState>(world_.block_entity(open_pos_))) {
            const float kx = x0 + 2 * (UI_SLOT + UI_GAP), ky = top + 20;
            const float flame = kiln->burn_total > 0.f ? kiln->burn_left / kiln->burn_total : 0.f;
            draw_bar(Rectangle{kx + (UI_SLOT + UI_GAP) / 2, ky + UI_SLOT + UI_GAP + 12, UI_SLOT, 10}, flame, ORANGE);
            draw_bar(Rectangle{kx + 2 * (UI_SLOT + UI_GAP) + 8, ky + UI_SLOT + UI_GAP + 15, 2 * UI_SLOT - 16, 10},
                     kiln->progress / SMELT_SECONDS, LIGHTGRAY);
        }
    if (screen_ == Screen::Inventory) {
        draw_label(mode_ == GameMode::Creative ? "Items" : "Crafting", x0 + grid_w + 20, top - 6, 20, WHITE);
        const float side_x = x0 + grid_w + 20, side_y = inv_y - 4 * (UI_SLOT + UI_GAP) - 4;
        if (mode_ == GameMode::Creative) {
            const std::vector<ItemId> palette = creative_palette();
            const int rows = (static_cast<int>(palette.size()) + PALETTE_COLS - 1) / PALETTE_COLS;
            scroll_ = clamp_scroll(scroll_, rows, PALETTE_ROWS);
            for (int i = 0; i < PALETTE_COLS * PALETTE_ROWS; ++i) {
                const std::size_t at = static_cast<std::size_t>(scroll_ * PALETTE_COLS + i);
                if (at >= palette.size()) break;
                const Rectangle r = grid_slot_rect(Vector2{side_x, side_y}, i, PALETTE_COLS, UI_SLOT, UI_GAP);
                const ItemStack s{palette[at], 1, 0};
                const bool over = CheckCollisionPointRec(m, r);
                draw_slot(t, s, r, false, over);
                if (over) draw_tooltip(item_label(s), m);
            }
        } else {
            const StationSet near =
                stations_near(world_, Vector3{player_.pos.x, player_.pos.y + PLAYER_H / 2, player_.pos.z});
            const std::vector<const Recipe*> list = available(inv_, near);
            scroll_ = clamp_scroll(scroll_, static_cast<int>(list.size()), RECIPE_ROWS);
            if (list.empty()) draw_label("Nothing to craft yet", side_x, side_y + 6, 16, LIGHTGRAY);
            for (int r = 0; r < RECIPE_ROWS; ++r) {
                const std::size_t at = static_cast<std::size_t>(scroll_ + r);
                if (at >= list.size()) break;
                const Rectangle row{side_x, side_y + static_cast<float>(r) * RECIPE_ROW, SIDE_W - 20, RECIPE_ROW - 2};
                const bool over = CheckCollisionPointRec(m, row);
                DrawRectangleRec(row, over ? Color{80, 80, 100, 200} : Color{0, 0, 0, 90});
                draw_item_icon(t, list[at]->output, Rectangle{row.x + 2, row.y + 2, row.height - 4, row.height - 4});
                const std::string name = std::string(item_def(list[at]->output.id).name);
                draw_label(name, row.x + row.height + 4, row.y + 2, 14, WHITE);
                draw_label(recipe_text(*list[at]), row.x + row.height + 4, row.y + 16, 10, LIGHTGRAY);
            }
            if (static_cast<int>(list.size()) > RECIPE_ROWS)
                draw_label("wheel: scroll   shift-click: craft many", side_x, side_y + RECIPE_ROWS * RECIPE_ROW + 4, 10,
                           LIGHTGRAY);
        }
    }
    if (hovered != nullptr && !hovered->empty() && cursor_.empty()) draw_tooltip(item_label(*hovered), m);
    if (!cursor_.empty()) draw_item_icon(t, cursor_, Rectangle{m.x - 16, m.y - 16, 32, 32});
}

void Game::draw(const RayHit& hit) {
    const int day_tick = time_.day_tick();
    const Color sky = sky_color(day_tick);
    const float day_light = daylight_at(day_tick);  // not `daylight`: glibc's <time.h> declares one
    renderer_.set_fog(fog_range(opts_.rd), sky);
    renderer_.set_daylight(day_light);
    const std::vector<SlotRef> slots = screen_slots();
    if (screen_ != Screen::None && !screen_fresh_) screen_click(slots);
    screen_fresh_ = false;

    BeginDrawing();
    ClearBackground(sky);
    if (spawned_) {
        const Vector3 eye = player_eye(player_);
        const Camera3D cam{eye, Vector3Add(eye, player_look(player_)), Vector3{0, 1, 0}, 70.f,
                           CAMERA_PERSPECTIVE};
        const int h = std::max(1, GetScreenHeight());
        BeginMode3D(cam);
        renderer_.draw_sky(cam, sun_angle(day_tick));
        drawn_ = renderer_.draw(cam, static_cast<float>(GetScreenWidth()) / static_cast<float>(h));
        for (const ItemEntity& e : items_.all())
            renderer_.draw_item(cam, e.stack.id, e.pos, ITEM_DRAW_SIZE, light_tint(world_, e.pos, day_light));
        if (hit.hit) {
            renderer_.draw_highlight(hit.x, hit.y, hit.z);
            if (mining_ && mode_ == GameMode::Survival) {
                const float t = break_time(world_.get(hit.x, hit.y, hit.z), inv_.held());
                if (t > 0.f && std::isfinite(t)) {
                    const int stage = std::min(3, static_cast<int>(mine_progress_ / t * 4.f));
                    renderer_.draw_crack(hit.x, hit.y, hit.z, stage);
                }
            }
        }
        EndMode3D();
        const bool eye_wet = eye_in_water(player_, world_);
        if (eye_wet) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), UNDERWATER);
        draw_hurt_flash(hurt_flash_ / HURT_FLASH_S);
        if (!dead_) draw_crosshair();
        draw_hotbar(textures(), inv_);
        if (mode_ == GameMode::Survival)
            draw_survival_hud(vitals_, armor_points(inv_), eye_wet || vitals_.air < MAX_AIR);
        if (mode_ == GameMode::Creative)
            DrawText(player_.flying ? "Creative - flying" : "Creative", GetScreenWidth() - 190, 12, 18, WHITE);
        if (toast_left_ > 0.f) draw_toast(toast_);
        if (screen_ != Screen::None) draw_screen(slots);
        const bool smoke_death = opts_.smoke && smoke_frames_ >= SMOKE_DEATH_FROM && smoke_frames_ < SMOKE_DEATH_TO;
        if (dead_ || smoke_death)
            draw_death_screen(death_note_, CheckCollisionPointRec(GetMousePosition(), death_button_rect()));
        if (respawning_) draw_centered_text("Respawning...", GetScreenHeight() / 2 - 15, 30, WHITE);
        if (debug_) draw_debug(player_, world_, mgr_, drawn_, time_, items_.all().size());
    } else {
        draw_centered_text("Generating world...", GetScreenHeight() / 2 - 15, 30, WHITE);
    }
    EndDrawing();
}

void Game::smoke_step(float dt) {
    if (!spawned_) return;
    smoke_max_frame_ = std::max(smoke_max_frame_, dt);
    // Turn when a wall or trunk has held the walker for half a second.
    if (smoke_frames_ % 30 == 0) {
        if (Vector3Distance(player_.pos, smoke_anchor_) < 1.f) player_.yaw += 1.2f;
        smoke_anchor_ = player_.pos;
    }
    // Show the inventory screen for a moment, so its drawing runs in the smoke test too.
    if (smoke_frames_ == SMOKE_UI_FROM) open_screen(Screen::Inventory);
    if (smoke_frames_ == SMOKE_UI_TO) close_screen();
    ++smoke_frames_;
}

bool Game::frame() {
    if (WindowShouldClose()) return false;
    const float dt = GetFrameTime();
    handle_input(dt);

    mgr_.update(!spawned_ ? spawn_col_ : (respawning_ ? respawn_col_ : column_at(player_.pos)));
    if (!spawned_ && mgr_.column_ready(spawn_col_)) {
        if (!meta_) player_ = spawn_player(world_);
        spawned_ = true;
    }
    update_respawn();

    RayHit hit{};
    if (spawned_) {
        const bool alive = !dead_ && !respawning_;
        if (alive) {
            hit = raycast_voxel(world_, player_eye(player_), player_look(player_), REACH);
            update_mining(hit, dt);
            use_block(hit);
            update_eating(dt);
        }
        items_.update(world_, dt);
        if (alive) items_.collect(player_.pos, inv_);
        tick_kilns(dt);
        if (screen_ == Screen::Kiln || screen_ == Screen::Crate) {
            const Vector3 c{open_pos_.x + 0.5f, open_pos_.y + 0.5f, open_pos_.z + 0.5f};
            if (world_.block_entity(open_pos_) == nullptr || Vector3Distance(player_eye(player_), c) > SCREEN_REACH)
                close_screen();
        }
    }

    for (const MeshResult& m : mgr_.take_meshes(MESH_UPLOADS_PER_FRAME)) renderer_.upload(m.pos, m.mesh);
    for (ChunkPos p : mgr_.take_removed()) renderer_.remove(p);

    draw(hit);

    if (opts_.smoke) {
        smoke_step(dt);
        if (smoke_frames_ == SMOKE_FRAMES) return false;
        if (!spawned_ && GetTime() - start_ > SMOKE_SPAWN_TIMEOUT_S) return false;
    }
    return true;
}

bool Game::save() {
    if (!store_) return true;  // --smoke writes nothing
    if (spawned_) {
        meta_ = WorldMeta{world_.seed(), player_.pos.x, player_.pos.y, player_.pos.z, player_.yaw, player_.pitch};
        meta_->time = time_.ticks;
    }
    if (meta_) {
        meta_->mode = mode_;
        meta_->inv = inv_;
        meta_->vitals = vitals_;
        meta_->spawn = bedroll_;
    }
    for (ColumnPos c : world_.loaded_columns())
        if (column_modified(world_, c)) store_->save_column(c, copy_column(world_, c));
    bool ok = true;
    if (!store_->flush()) {
        std::fputs("nimecraft: could not write some region files\n", stderr);
        ok = false;
    }
    if (meta_ && !save_meta(meta_file_, *meta_)) {
        std::fputs("nimecraft: could not write world.meta\n", stderr);
        ok = false;
    }
    return ok;
}

int Game::finish() {
    close_screen();  // a stack on the cursor goes back into the inventory before saving
    if (window_open_) {
        renderer_.shutdown();
        CloseWindow();
        window_open_ = false;
    }
    if (opts_.smoke) {
        if (smoke_frames_ < SMOKE_FRAMES) {
            std::fprintf(stderr, "smoke failed: frames=%d of %d\n", smoke_frames_, SMOKE_FRAMES);
            return 1;
        }
        std::printf("smoke ok frames=%d max_frame_ms=%d\n", smoke_frames_,
                    static_cast<int>(std::ceil(smoke_max_frame_ * 1000.f)));
        return 0;
    }
    return save() ? 0 : 1;
}
