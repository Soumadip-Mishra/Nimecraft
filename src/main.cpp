// Game entry point: command line, world load and save, the frame loop, input, and the HUD.
// Decisions live in the tested logic modules; this file only wires them to the window.
#include "block.hpp"
#include "chunk_manager.hpp"
#include "cli.hpp"
#include "coords.hpp"
#include "jobs.hpp"
#include "platform.hpp"
#include "player.hpp"
#include "raycast.hpp"
#include "render.hpp"
#include "storage.hpp"
#include "world.hpp"
#include "world_time.hpp"

#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>

namespace {

// Creative-style block choices until the M4 inventory; TORCH and LANTERN from M3.
constexpr BlockId HOTBAR[] = {blk::GRASS,  blk::DIRT,   blk::STONE, blk::SAND,   blk::LOG,
                              blk::LEAVES, blk::PLANKS, blk::TORCH, blk::LANTERN};
constexpr int HOTBAR_SIZE = static_cast<int>(sizeof HOTBAR / sizeof HOTBAR[0]);
constexpr double DEBUG_TIME_STEP = 1000;  // F4 skips this many ticks
constexpr Color UNDERWATER{20, 60, 160, 110};  // screen tint while the eye is in WATER
constexpr int SMOKE_FRAMES = 600;
constexpr double SMOKE_SPAWN_TIMEOUT_S = 60.0;
constexpr std::size_t MESH_UPLOADS_PER_FRAME = 16;

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

// Saves every loaded column with a modified chunk, flushes the regions, and writes world.meta.
bool save_world(const World& w, RegionStore& store, const std::filesystem::path& meta_file,
                const std::optional<WorldMeta>& meta) {
    for (ColumnPos c : w.loaded_columns())
        if (column_modified(w, c)) store.save_column(c, copy_column(w, c));
    bool ok = true;
    if (!store.flush()) {
        std::fputs("nimecraft: could not write some region files\n", stderr);
        ok = false;
    }
    if (meta && !save_meta(meta_file, *meta)) {
        std::fputs("nimecraft: could not write world.meta\n", stderr);
        ok = false;
    }
    return ok;
}

std::filesystem::path world_dir(const std::string& name) {
    // Command-line text is UTF-8 on Linux; build the path from UTF-8 so non-ASCII names work.
    const std::u8string utf8(reinterpret_cast<const char8_t*>(name.data()), name.size());
    return user_data_dir() / "saves" / std::filesystem::path(utf8);
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
    return in;
}

int update_hotbar(int selected) {
    for (int i = 0; i < HOTBAR_SIZE; ++i)
        if (IsKeyPressed(KEY_ONE + i)) selected = i;
    const float wheel = GetMouseWheelMove();
    if (wheel > 0) selected = (selected + HOTBAR_SIZE - 1) % HOTBAR_SIZE;
    if (wheel < 0) selected = (selected + 1) % HOTBAR_SIZE;
    return selected;
}

void draw_centered_text(const char* text, int y, int size, Color c) {
    DrawText(text, (GetScreenWidth() - MeasureText(text, size)) / 2, y, size, c);
}

void draw_hud(int selected) {
    const int cx = GetScreenWidth() / 2, cy = GetScreenHeight() / 2;
    DrawRectangle(cx - 9, cy - 1, 18, 2, WHITE);
    DrawRectangle(cx - 1, cy - 9, 2, 18, WHITE);

    constexpr int SLOT = 64, GAP = 4;
    const int total = HOTBAR_SIZE * SLOT + (HOTBAR_SIZE - 1) * GAP;
    const int x0 = (GetScreenWidth() - total) / 2, y0 = GetScreenHeight() - SLOT - 12;
    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        const int x = x0 + i * (SLOT + GAP);
        DrawRectangle(x, y0, SLOT, SLOT, Color{0, 0, 0, 120});
        if (i == selected) DrawRectangleLines(x - 2, y0 - 2, SLOT + 4, SLOT + 4, WHITE);
        const std::string name(block_def(HOTBAR[i]).name);
        DrawText(std::to_string(i + 1).c_str(), x + 4, y0 + 4, 10, LIGHTGRAY);
        DrawText(name.c_str(), x + (SLOT - MeasureText(name.c_str(), 10)) / 2, y0 + SLOT / 2 - 5, 10,
                 WHITE);
    }
}

void draw_debug(const Player& p, const World& w, const ChunkManager& mgr, int drawn, const WorldTime& t) {
    constexpr int LINES = 6;
    char lines[LINES][96];
    std::snprintf(lines[0], sizeof lines[0], "FPS %d", GetFPS());
    std::snprintf(lines[1], sizeof lines[1], "XYZ %.2f %.2f %.2f", static_cast<double>(p.pos.x),
                  static_cast<double>(p.pos.y), static_cast<double>(p.pos.z));
    std::snprintf(lines[2], sizeof lines[2], "Loaded columns %zu", w.loaded_columns().size());
    std::snprintf(lines[3], sizeof lines[3], "Pending jobs %zu", mgr.pending_jobs());
    std::snprintf(lines[4], sizeof lines[4], "Chunks drawn %d", drawn);
    std::snprintf(lines[5], sizeof lines[5], "Time %d %s (F4 +1000)", t.day_tick(), t.is_night() ? "night" : "day");
    DrawRectangle(6, 6, 300, LINES * 20 + 8, Color{0, 0, 0, 140});
    for (int i = 0; i < LINES; ++i) DrawText(lines[i], 12, 12 + i * 20, 18, WHITE);
}

int run(const CliOptions& o) {
    // --smoke plays a throwaway world: no save folder, no store, nothing written.
    std::optional<RegionStore> store;
    std::filesystem::path meta_file;
    std::optional<WorldMeta> meta;
    if (!o.smoke) {
        const std::filesystem::path dir = world_dir(o.world);
        store.emplace(dir);
        meta_file = dir / "world.meta";
        meta = load_meta(meta_file);
    }
    const uint32_t seed =
        meta ? meta->seed : o.seed.value_or(o.smoke ? 1u : static_cast<uint32_t>(std::time(nullptr)));

    World world(seed);
    const unsigned hw = std::thread::hardware_concurrency();
    ThreadPool pool(hw > 1 ? hw - 1 : 1);
    ChunkManager mgr(world, pool, store ? &*store : nullptr, o.rd);

    Player player{};
    WorldTime world_time;
    ColumnPos spawn_col{0, 0};
    if (meta) {
        world_time.ticks = meta->time;
        player.pos = Vector3{meta->px, meta->py, meta->pz};
        player.yaw = meta->yaw;
        player.pitch = meta->pitch;
        spawn_col = column_at(player.pos);
    }

    // --smoke paces itself at 60 FPS instead of vsync: a compositor throttles a vsynced window it
    // considers hidden to about 1 frame per second, which would measure the desktop, not the game.
    // It also keeps running if the compositor iconifies the window; raylib otherwise blocks in
    // EndDrawing until the next window event, and the smoke run never finishes.
    const unsigned pacing = o.smoke ? static_cast<unsigned>(FLAG_WINDOW_ALWAYS_RUN)
                                    : static_cast<unsigned>(FLAG_VSYNC_HINT);
    SetConfigFlags(pacing | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Nimecraft");
    if (o.smoke) SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    Renderer renderer;
    renderer.init();

    bool captured = !o.smoke;
    if (captured) DisableCursor();
    bool spawned = false, debug = false;
    int selected = 0, drawn = 0, smoke_frames = 0;
    float smoke_max_frame = 0.f;
    Vector3 smoke_anchor{};
    const double start = GetTime();

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();

        // Esc releases the cursor; the click that recaptures it does nothing else.
        bool swallow_click = false;
        if (!o.smoke) {
            if (captured && IsKeyPressed(KEY_ESCAPE)) {
                EnableCursor();
                captured = false;
            } else if (!captured && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                                     IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
                DisableCursor();
                captured = true;
                swallow_click = true;
            }
        }
        if (IsKeyPressed(KEY_F3)) debug = !debug;
        if (IsKeyPressed(KEY_F4)) world_time.ticks += DEBUG_TIME_STEP;
        selected = update_hotbar(selected);

        PlayerInput in = read_input(captured && !swallow_click);
        if (o.smoke) {
            in.forward = 1.f;
            in.jump = true;  // hop up 1-block steps so the walk keeps streaming new terrain
        }
        if (spawned) {
            player_update(player, world, in, dt);
            world_time.advance(dt);
        }
        mgr.update(spawned ? column_at(player.pos) : spawn_col);
        if (!spawned && mgr.column_ready(spawn_col)) {
            if (!meta) player = spawn_player(world);
            spawned = true;
        }

        RayHit hit{};
        if (spawned) hit = raycast_voxel(world, player_eye(player), player_look(player), REACH);
        if (spawned && captured && !swallow_click && hit.hit) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (block_def(world.get(hit.x, hit.y, hit.z)).breakable &&
                    world.set(hit.x, hit.y, hit.z, blk::AIR))
                    mgr.flush_edits();
            } else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                const int x = hit.x + hit.nx, y = hit.y + hit.ny, z = hit.z + hit.nz;
                const bool has_face = hit.nx != 0 || hit.ny != 0 || hit.nz != 0;
                if (has_face && world.get(x, y, z) == blk::AIR &&
                    !player_intersects_block(player, x, y, z) && world.set(x, y, z, HOTBAR[selected]))
                    mgr.flush_edits();
            }
        }

        for (const MeshResult& m : mgr.take_meshes(MESH_UPLOADS_PER_FRAME)) renderer.upload(m.pos, m.mesh);
        for (ChunkPos p : mgr.take_removed()) renderer.remove(p);

        const int day_tick = world_time.day_tick();
        const Color sky = sky_color(day_tick);
        renderer.set_fog(fog_range(o.rd), sky);
        renderer.set_daylight(daylight_at(day_tick));

        BeginDrawing();
        ClearBackground(sky);
        if (spawned) {
            const Vector3 eye = player_eye(player);
            const Camera3D cam{eye, Vector3Add(eye, player_look(player)), Vector3{0, 1, 0}, 70.f,
                               CAMERA_PERSPECTIVE};
            const int h = std::max(1, GetScreenHeight());
            BeginMode3D(cam);
            renderer.draw_sky(cam, sun_angle(day_tick));
            drawn = renderer.draw(cam, static_cast<float>(GetScreenWidth()) / static_cast<float>(h));
            if (hit.hit) renderer.draw_highlight(hit.x, hit.y, hit.z);
            EndMode3D();
            if (eye_in_water(player, world)) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), UNDERWATER);
            draw_hud(selected);
            if (debug) draw_debug(player, world, mgr, drawn, world_time);
        } else {
            draw_centered_text("Generating world...", GetScreenHeight() / 2 - 15, 30, WHITE);
        }
        EndDrawing();

        if (o.smoke) {
            if (spawned) {
                smoke_max_frame = std::max(smoke_max_frame, dt);
                // Turn when a wall or trunk has held the walker for half a second.
                if (smoke_frames % 30 == 0) {
                    if (Vector3Distance(player.pos, smoke_anchor) < 1.f) player.yaw += 1.2f;
                    smoke_anchor = player.pos;
                }
                if (++smoke_frames == SMOKE_FRAMES) break;
            } else if (GetTime() - start > SMOKE_SPAWN_TIMEOUT_S) {
                break;
            }
        }
    }

    renderer.shutdown();
    CloseWindow();

    if (o.smoke) {
        if (smoke_frames < SMOKE_FRAMES) {
            std::fprintf(stderr, "smoke failed: frames=%d of %d\n", smoke_frames, SMOKE_FRAMES);
            return 1;
        }
        std::printf("smoke ok frames=%d max_frame_ms=%d\n", smoke_frames,
                    static_cast<int>(std::ceil(smoke_max_frame * 1000.f)));
        return 0;
    }
    if (spawned) {
        meta = WorldMeta{seed, player.pos.x, player.pos.y, player.pos.z, player.yaw, player.pitch};
        meta->time = world_time.ticks;
    }
    return save_world(world, *store, meta_file, meta) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t count = argc > 1 ? static_cast<std::size_t>(argc - 1) : 0;
    const std::optional<CliOptions> opts = parse_cli(std::span<const char* const>(argv + 1, count));
    if (!opts) {
        std::fputs(cli_usage(), stderr);
        return 2;
    }
    try {
        return run(*opts);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nimecraft: %s\n", e.what());
        return 1;
    }
}
