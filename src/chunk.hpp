// 16^3 block storage with an edit version and a modified flag.
#pragma once

#include "block.hpp"
#include "coords.hpp"

#include <array>
#include <cstdint>
#include <memory>

struct Chunk {
    ChunkPos pos{};
    std::array<BlockId, CHUNK * CHUNK * CHUNK> blocks{};
    uint32_t version = 0;   // incremented by every set()
    bool modified = false;  // set by World::set, persisted when true

    // Local coordinates are in [0, CHUNK); the caller guarantees the range.
    static int index(int lx, int ly, int lz);  // lx + lz*16 + ly*256
    BlockId get(int lx, int ly, int lz) const;
    void set(int lx, int ly, int lz, BlockId b);
    bool is_empty() const;  // all AIR
};

using Column = std::array<std::unique_ptr<Chunk>, SECTIONS>;  // index = chunk y
