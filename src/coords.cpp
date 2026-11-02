#include "coords.hpp"

#include <cstdint>

namespace {

// splitmix64 finalizer: a bijective 64-bit mix, the same on every toolchain.
uint64_t mix64(uint64_t v) {
    v ^= v >> 30;
    v *= 0xBF58476D1CE4E5B9ULL;
    v ^= v >> 27;
    v *= 0x94D049BB133111EBULL;
    v ^= v >> 31;
    return v;
}

uint64_t pack(int a, int b) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
}

}  // namespace

std::size_t ColumnPosHash::operator()(ColumnPos p) const noexcept {
    return static_cast<std::size_t>(mix64(pack(p.x, p.z)));
}

std::size_t ChunkPosHash::operator()(ChunkPos p) const noexcept {
    return static_cast<std::size_t>(mix64(mix64(pack(p.x, p.z)) + static_cast<uint32_t>(p.y)));
}

int floor_div(int a, int b) {
    int q = a / b;
    if (a % b != 0 && a < 0) --q;  // C++ division truncates toward zero
    return q;
}

int floor_mod(int a, int b) {
    int r = a % b;
    return r < 0 ? r + b : r;
}

ChunkPos chunk_of(int x, int y, int z) {
    return ChunkPos{floor_div(x, CHUNK), floor_div(y, CHUNK), floor_div(z, CHUNK)};
}

ColumnPos column_of(ChunkPos c) {
    return ColumnPos{c.x, c.z};
}
