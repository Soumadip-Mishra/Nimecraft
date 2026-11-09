// Region files that hold player-modified columns, and the world.meta text file.
//
// Region file r.<rx>.<rz>.bin, rx = floor_div(cx, 32), little-endian:
//   magic "NMCR", u32 version, u32 count, then per entry
//   i32 cx, i32 cz, u32 len, len bytes of RLE pairs (u16 run >= 1, u16 id < blk::COUNT)
//   covering exactly 16 * 4096 blocks (chunk 0 first, each chunk in Chunk::index order),
//   then (version 2, M4) the column's block entities: u32 be_count, per entity i32 x, y, z, u8 kind
//   (0 kiln, 1 crate, 2 cairn), payload (see storage.cpp),
//   then (version 3, M6) its passive creatures: u32 mob_count, per mob u8 type, f32 x, y, z, yaw,
//   health (Chunk::mobs of every chunk, in chunk order; hostile types are never written, and a
//   hostile or unknown type, a non-finite float, health <= 0, feet outside the column, or feet y
//   outside -64..320 makes the file corrupt, and such a creature is never written).
//   then (version 4, M7) its block meta: u32 len, then len bytes holding, for each of the 16
//   chunks in order, RLE pairs (u16 run >= 1, u8 value) of Chunk::meta covering exactly 4096 cells.
//   Version 4 is written; versions 1 to 4 load (v1 columns have no block entities, v1 and v2
//   columns no creatures, v1 to v3 columns meta 0). A loaded creature goes into the chunk holding
//   its feet (y clamped).
// world.meta: "key=value" lines: version=1, seed, px, py, pz, yaw, pitch, then the optional keys
// added later (time, M3; mode, inv, armor, sel, M4; vitals, spawn, M5). Unknown keys are ignored; non-finite floats or missing required keys
// make the file invalid.
#pragma once

#include "chunk.hpp"
#include "coords.hpp"
#include "inventory.hpp"
#include "player.hpp"
#include "vitals.hpp"

#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

class ThreadPool;

// Internal to RegionStore: one column's validated bytes, RLE blocks, its entity section, its
// creature section, and its meta section.
struct RegionColumn {
    std::vector<uint8_t> blocks, entities, mobs, meta;
};

class RegionStore {
public:
    explicit RegionStore(std::filesystem::path dir);  // creates dir
    ~RegionStore();  // waits for a flush_async write still in flight
    RegionStore(const RegionStore&) = delete;
    RegionStore& operator=(const RegionStore&) = delete;
    // Thread-safe. nullopt if absent or its region file is corrupt; chunks are marked modified and
    // hold the column's block entities.
    std::optional<Column> load_column(ColumnPos c);
    void save_column(ColumnPos c, const Column& col);  // thread-safe, cached until flush
    // Rewrites changed regions (tmp file + rename), after waiting for a flush_async in flight; false
    // if any write failed (those regions stay changed, so a later flush tries again).
    bool flush();
    // Background autosave (M8 Task 5): waits for the previous flush_async, takes a snapshot of every
    // changed region on the calling thread (so later save_column calls are not in it), and writes it
    // in a pool job. The future is true when every write succeeded (or nothing had changed: no file
    // is written); on a failure the failed regions count as changed again, their data stays in
    // memory, and a later flush or flush_async writes them.
    std::future<bool> flush_async(ThreadPool& pool);

private:
    using Key = std::pair<int, int>;                      // (x, z), ordered for stable output
    struct Region {
        std::map<Key, RegionColumn> columns;
        bool dirty = false;
    };

    Region& region_locked(Key r);  // loads the file on first use; mutex_ held
    std::filesystem::path region_path(Key r) const;
    // The file bytes of every dirty region, which are then marked clean; mutex_ held.
    std::vector<std::pair<Key, std::vector<uint8_t>>> take_dirty_locked();
    void wait_for_writer();

    std::filesystem::path dir_;
    std::mutex mutex_;
    std::map<Key, Region> regions_;
    std::shared_future<void> writing_;  // the flush_async job in flight, if any (main thread only)
};

struct WorldMeta {
    uint32_t seed;
    float px, py, pz, yaw, pitch;
    // Key `time` (M3): WorldTime::ticks. Optional; missing, non-finite, negative, or unparsable
    // values load as 6000 without rejecting the file.
    double time = 6000;
    // Keys added in M4 Task 6, each optional: `mode` survival|creative (anything else: Survival);
    // `inv` 36 and `armor` 4 comma-separated id:count:damage entries (empty slot 0:0:0); `sel` the
    // hotbar slot 0..8. An entry that is not a valid stack (unknown id, count 0 or above
    // max_stack, damage at or above a tool's durability or on a non-tool, or an armor entry of the
    // wrong slot) loads as an empty slot; missing entries are empty; a bad sel loads as 0.
    GameMode mode = GameMode::Survival;
    Inventory inv{};
    // Keys added in M5 Task 3, each optional: `vitals` health,hunger,saturation,air,exhaustion,
    // regen_timer,starve_timer (missing, a wrong field count, or any non-finite or unparsable
    // field: full vitals; values clamped to their ranges: health, hunger 0..20, saturation 0..hunger,
    // air 0..10, exhaustion 0..10 (regeneration's +6 waits a tick to drain), timers 0..4); `spawn`
    // the bedroll cell x,y,z (written only when set; missing or invalid: none, the world spawn).
    Vitals vitals{};
    std::optional<BlockPos> spawn{};
};

// The whole file in binary mode; nullopt when it cannot be opened or read.
std::optional<std::vector<uint8_t>> read_file(const std::filesystem::path& p);
// Writes "<file>.tmp" and renames it over file, so a crash never leaves a half-written file;
// false (and no tmp file left behind) when either step fails. Never throws.
bool write_file_atomic(const std::filesystem::path& file, const std::vector<uint8_t>& bytes);

bool save_meta(const std::filesystem::path& file, const WorldMeta& m);
std::optional<WorldMeta> load_meta(const std::filesystem::path& file);
