// World constants, chunk and column positions, their hashes, and floor division.
#pragma once

#include <cstddef>

constexpr int CHUNK = 16, WORLD_HEIGHT = 256, SECTIONS = 16, SEA_LEVEL = 64;

// A 16^3 chunk; y is the section index 0..SECTIONS-1 inside its column.
struct ChunkPos {
    int x, y, z;
    bool operator==(const ChunkPos&) const = default;
};

// A column of SECTIONS chunks stacked from y = 0 to WORLD_HEIGHT - 1.
struct ColumnPos {
    int x, z;
    bool operator==(const ColumnPos&) const = default;
};

struct ChunkPosHash {
    std::size_t operator()(ChunkPos p) const noexcept;
};
struct ColumnPosHash {
    std::size_t operator()(ColumnPos p) const noexcept;
};

int floor_div(int a, int b);  // rounds toward -infinity, b > 0
int floor_mod(int a, int b);  // result in [0, b), b > 0

ChunkPos chunk_of(int x, int y, int z);  // chunk holding world block (x, y, z)
ColumnPos column_of(ChunkPos c);
