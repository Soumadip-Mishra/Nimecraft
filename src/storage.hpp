// Region files that hold player-modified columns, and the world.meta text file.
//
// Region file r.<rx>.<rz>.bin, rx = floor_div(cx, 32), little-endian:
//   magic "NMCR", u32 version = 1, u32 count, then per entry
//   i32 cx, i32 cz, u32 len, len bytes of RLE pairs (u16 run >= 1, u16 id < blk::COUNT)
//   covering exactly 16 * 4096 blocks (chunk 0 first, each chunk in Chunk::index order).
// world.meta: "key=value" lines: version=1, seed, px, py, pz, yaw, pitch, then the optional keys
// added later (time, M3). Unknown keys are ignored; non-finite floats or missing required keys
// make the file invalid.
#pragma once

#include "chunk.hpp"
#include "coords.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

class RegionStore {
public:
    explicit RegionStore(std::filesystem::path dir);  // creates dir
    // Thread-safe. nullopt if absent or its region file is corrupt; chunks are marked modified.
    std::optional<Column> load_column(ColumnPos c);
    void save_column(ColumnPos c, const Column& col);  // thread-safe, cached until flush
    bool flush();  // rewrite changed regions: tmp file + rename; false if any write failed

private:
    using Key = std::pair<int, int>;                      // (x, z), ordered for stable output
    struct Region {
        std::map<Key, std::vector<uint8_t>> columns;      // column -> RLE bytes
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
};

bool save_meta(const std::filesystem::path& file, const WorldMeta& m);
std::optional<WorldMeta> load_meta(const std::filesystem::path& file);
