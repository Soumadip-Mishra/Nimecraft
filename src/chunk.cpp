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

uint8_t sky_light(const Chunk& c, int i) {
    return static_cast<uint8_t>(c.light[static_cast<std::size_t>(i)] >> 4);
}

void set_sky_light(Chunk& c, int i, uint8_t v) {
    uint8_t& cell = c.light[static_cast<std::size_t>(i)];
    cell = static_cast<uint8_t>((cell & 0x0F) | ((v & 0x0F) << 4));
}

uint8_t block_light(const Chunk& c, int i) {
    return static_cast<uint8_t>(c.light[static_cast<std::size_t>(i)] & 0x0F);
}

void set_block_light(Chunk& c, int i, uint8_t v) {
    uint8_t& cell = c.light[static_cast<std::size_t>(i)];
    cell = static_cast<uint8_t>((cell & 0xF0) | (v & 0x0F));
}

bool Chunk::is_empty() const {
    return std::all_of(blocks.begin(), blocks.end(), [](BlockId b) { return b == blk::AIR; });
}
