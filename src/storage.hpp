// Region files that hold player-modified columns, and the world.meta text file.
//
// Region file r.<rx>.<rz>.bin, rx = floor_div(cx, 32), little-endian:
//   magic "NMCR", u32 version, u32 count, then per entry
//   i32 cx, i32 cz, u32 len, len bytes of RLE pairs (u16 run >= 1, u16 id < blk::COUNT)
//   covering exactly 16 * 4096 blocks (chunk 0 first, each chunk in Chunk::index order),
//   then (version 2, M4) the column's block entities: u32 be_count, per entity i32 x, y, z, u8 kind
//   (0 kiln, 1 crate, 2 cairn), payload (see storage.cpp). Version 2 is written; versions 1 and 2
//   load (v1 columns have no block entities).
// world.meta: "key=value" lines: version=1, seed, px, py, pz, yaw, pitch, then the optional keys
// added later (time, M3; mode, inv, armor, sel, M4). Unknown keys are ignored; non-finite floats or missing required keys
// make the file invalid.
#pragma once

#include "chunk.hpp"
#include "coords.hpp"
#include "inventory.hpp"
#include "player.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

// Internal to RegionStore: one column's validated bytes, RLE blocks and its entity section.
struct RegionColumn {
    std::vector<uint8_t> blocks, entities;
};

class RegionStore {
public:
    explicit RegionStore(std::filesystem::path dir);  // creates dir
    // Thread-safe. nullopt if absent or its region file is corrupt; chunks are marked modified and
    // hold the column's block entities.
    std::optional<Column> load_column(ColumnPos c);
    void save_column(ColumnPos c, const Column& col);  // thread-safe, cached until flush
    bool flush();  // rewrite changed regions: tmp file + rename; false if any write failed

private:
    using Key = std::pair<int, int>;                      // (x, z), ordered for stable output
    struct Region {
        std::map<Key, RegionColumn> columns;
        bool dirty = false;
    };

    Region& region_locked(Key r);  // loads the file on first use; mutex_ held
    std::filesystem::path region_path(Key r) const;

    std::filesystem::path dir_;
    std::mutex mutex_;
    std::map<Key, Region> regions_;
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
};

bool save_meta(const std::filesystem::path& file, const WorldMeta& m);
std::optional<WorldMeta> load_meta(const std::filesystem::path& file);
