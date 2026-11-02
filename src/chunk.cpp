#include "chunk.hpp"

#include <algorithm>
#include <cstddef>

int Chunk::index(int lx, int ly, int lz) {
    return lx + lz * CHUNK + ly * CHUNK * CHUNK;
}

BlockId Chunk::get(int lx, int ly, int lz) const {
    return blocks[static_cast<std::size_t>(index(lx, ly, lz))];
}

void Chunk::set(int lx, int ly, int lz, BlockId b) {
    blocks[static_cast<std::size_t>(index(lx, ly, lz))] = b;
    ++version;
}

bool Chunk::is_empty() const {
    return std::all_of(blocks.begin(), blocks.end(), [](BlockId b) { return b == blk::AIR; });
}
