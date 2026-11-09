// 16^3 block storage with an edit version and a modified flag.
#pragma once

#include "block.hpp"
#include "block_entities.hpp"
#include "coords.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

// A passive creature stored with its column (region v3, M6 Task 6): MobType id, feet position,
// yaw, health. Plain data, so chunk storage needs no creature code.
struct SavedMob {
    uint8_t type;
    float x, y, z, yaw, health;
};

struct Chunk {
    ChunkPos pos{};
    std::array<BlockId, CHUNK * CHUNK * CHUNK> blocks{};
    uint32_t version = 0;   // incremented by every set()
    bool modified = false;  // set by World::set, persisted when true
    // Per cell, in Chunk::index order: high nibble sky light, low nibble block light. Not
    // persisted (recomputed when a column loads) and not counted as an edit by version.
    std::array<uint8_t, CHUNK * CHUNK * CHUNK> light{};
    // Block entities by Chunk::index (M4 Task 5): kilns, crates, cairns. Persisted (region v2);
    // ordered so saves come out the same every time.
    std::map<uint16_t, BlockEntity> entities;
    // Saved creatures whose feet are in this chunk (region v3). Filled when a column is saved or
    // loaded; World::insert_column moves them out to World::take_arrived_mobs.
    std::vector<SavedMob> mobs;

    // Local coordinates are in [0, CHUNK); the caller guarantees the range.
    static int index(int lx, int ly, int lz);  // lx + lz*16 + ly*256
    BlockId get(int lx, int ly, int lz) const;
    void set(int lx, int ly, int lz, BlockId b);
    bool is_empty() const;  // all AIR
};

// Light levels of cell i (a Chunk::index in [0, 4096)), each 0..15. A setter keeps only the low 4
// bits of v and never changes the other channel.
uint8_t sky_light(const Chunk& c, int i);
void set_sky_light(Chunk& c, int i, uint8_t v);
uint8_t block_light(const Chunk& c, int i);
void set_block_light(Chunk& c, int i, uint8_t v);

using Column = std::array<std::unique_ptr<Chunk>, SECTIONS>;  // index = chunk y
