#include "world.hpp"

#include <cstddef>
#include <memory>
#include <utility>

World::World(uint32_t seed) : seed_(seed) {}

uint32_t World::seed() const {
    return seed_;
}

bool World::column_loaded(ColumnPos c) const {
    return columns_.find(c) != columns_.end();
}

void World::insert_column(ColumnPos c, Column col) {
    for (std::size_t i = 0; i < col.size(); ++i) {
        if (!col[i]) col[i] = std::make_unique<Chunk>();
        col[i]->pos = ChunkPos{c.x, static_cast<int>(i), c.z};
        dirty_.insert(col[i]->pos);
    }
    columns_.insert_or_assign(c, std::move(col));
}

Column World::remove_column(ColumnPos c) {
    auto it = columns_.find(c);
    if (it == columns_.end()) return Column{};
    Column out = std::move(it->second);
    columns_.erase(it);
    for (int y = 0; y < SECTIONS; ++y) dirty_.erase(ChunkPos{c.x, y, c.z});
    return out;
}

std::vector<ColumnPos> World::loaded_columns() const {
    std::vector<ColumnPos> out;
    out.reserve(columns_.size());
    for (const auto& entry : columns_) out.push_back(entry.first);
    return out;
}

const Chunk* World::chunk(ChunkPos p) const {
    if (p.y < 0 || p.y >= SECTIONS) return nullptr;
    auto it = columns_.find(column_of(p));
    if (it == columns_.end()) return nullptr;
    return it->second[static_cast<std::size_t>(p.y)].get();
}

Chunk* World::chunk(ChunkPos p) {
    return const_cast<Chunk*>(std::as_const(*this).chunk(p));
}

BlockId World::get(int x, int y, int z) const {
    if (y < 0) return blk::DEEPSTONE;
    if (y >= WORLD_HEIGHT) return blk::AIR;
    const Chunk* c = chunk(chunk_of(x, y, z));
    if (c == nullptr) return blk::AIR;
    return c->get(floor_mod(x, CHUNK), floor_mod(y, CHUNK), floor_mod(z, CHUNK));
}

bool World::set(int x, int y, int z, BlockId b) {
    if (y < 0 || y >= WORLD_HEIGHT) return false;
    const ChunkPos p = chunk_of(x, y, z);
    Chunk* c = chunk(p);
    if (c == nullptr) return false;
    const int lx = floor_mod(x, CHUNK), ly = floor_mod(y, CHUNK), lz = floor_mod(z, CHUNK);
    if (c->get(lx, ly, lz) == b) return true;

    c->set(lx, ly, lz, b);
    c->modified = true;
    dirty_.insert(p);
    // A block on a chunk face is visible from the chunk across that face: its mesh is stale too.
    if (lx == 0) mark_dirty_if_loaded(ChunkPos{p.x - 1, p.y, p.z});
    if (lx == CHUNK - 1) mark_dirty_if_loaded(ChunkPos{p.x + 1, p.y, p.z});
    if (ly == 0) mark_dirty_if_loaded(ChunkPos{p.x, p.y - 1, p.z});
    if (ly == CHUNK - 1) mark_dirty_if_loaded(ChunkPos{p.x, p.y + 1, p.z});
    if (lz == 0) mark_dirty_if_loaded(ChunkPos{p.x, p.y, p.z - 1});
    if (lz == CHUNK - 1) mark_dirty_if_loaded(ChunkPos{p.x, p.y, p.z + 1});
    return true;
}

std::vector<ChunkPos> World::take_dirty() {
    std::vector<ChunkPos> out(dirty_.begin(), dirty_.end());
    dirty_.clear();
    return out;
}

void World::mark_dirty_if_loaded(ChunkPos p) {
    if (chunk(p) != nullptr) dirty_.insert(p);
}
