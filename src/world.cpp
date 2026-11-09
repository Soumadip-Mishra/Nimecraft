#include "world.hpp"

#include "light.hpp"

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
        std::vector<SavedMob>& mobs = col[i]->mobs;
        arrived_mobs_.insert(arrived_mobs_.end(), mobs.begin(), mobs.end());
        mobs.clear();
    }
    columns_.insert_or_assign(c, std::move(col));
}

std::vector<SavedMob> World::take_arrived_mobs() {
    std::vector<SavedMob> out;
    out.swap(arrived_mobs_);
    return out;
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
    const BlockId before = c->get(lx, ly, lz);
    if (before == b) return true;

    c->set(lx, ly, lz, b);
    c->modified = true;
    c->entities.erase(static_cast<uint16_t>(Chunk::index(lx, ly, lz)));
    // Every chunk whose mesh snapshot (the chunk plus a 1-cell border) holds the cell is stale:
    // face culling reads the face neighbors, and ambient occlusion the edge and corner ones too.
    const int x0 = lx == 0 ? -1 : 0, x1 = lx == CHUNK - 1 ? 1 : 0;
    const int y0 = ly == 0 ? -1 : 0, y1 = ly == CHUNK - 1 ? 1 : 0;
    const int z0 = lz == 0 ? -1 : 0, z1 = lz == CHUNK - 1 ? 1 : 0;
    for (int dy = y0; dy <= y1; ++dy)
        for (int dz = z0; dz <= z1; ++dz)
            for (int dx = x0; dx <= x1; ++dx) mark_dirty(ChunkPos{p.x + dx, p.y + dy, p.z + dz});
    relight_block(*this, x, y, z, before, b);
    return true;
}

std::vector<ChunkPos> World::take_dirty() {
    std::vector<ChunkPos> out(dirty_.begin(), dirty_.end());
    dirty_.clear();
    return out;
}

void World::mark_dirty(ChunkPos p) {
    if (chunk(p) != nullptr) dirty_.insert(p);
}

namespace {

// The chunk holding p and p's Chunk::index there, or nullptr when y is out of range.
template <class W>
auto entity_slot(W& w, BlockPos p) -> decltype(w.chunk(ChunkPos{})) {
    if (p.y < 0 || p.y >= WORLD_HEIGHT) return nullptr;
    return w.chunk(chunk_of(p.x, p.y, p.z));
}

uint16_t entity_index(BlockPos p) {
    return static_cast<uint16_t>(Chunk::index(floor_mod(p.x, CHUNK), floor_mod(p.y, CHUNK), floor_mod(p.z, CHUNK)));
}

}  // namespace

const BlockEntity* World::block_entity(BlockPos p) const {
    const Chunk* c = entity_slot(*this, p);
    if (c == nullptr) return nullptr;
    auto it = c->entities.find(entity_index(p));
    return it == c->entities.end() ? nullptr : &it->second;
}

BlockEntity* World::block_entity(BlockPos p) {
    return const_cast<BlockEntity*>(std::as_const(*this).block_entity(p));
}

void World::set_block_entity(BlockPos p, BlockEntity be) {
    Chunk* c = entity_slot(*this, p);
    if (c == nullptr) return;
    c->entities.insert_or_assign(entity_index(p), std::move(be));
    c->modified = true;
}

void World::remove_block_entity(BlockPos p) {
    Chunk* c = entity_slot(*this, p);
    if (c != nullptr && c->entities.erase(entity_index(p)) > 0) c->modified = true;
}

std::vector<BlockPos> World::block_entity_positions() const {
    std::vector<BlockPos> out;
    for (const auto& [pos, col] : columns_)
        for (const auto& c : col) {
            if (!c || c->entities.empty()) continue;
            for (const auto& entry : c->entities) {
                const int i = entry.first;
                out.push_back(BlockPos{pos.x * CHUNK + i % CHUNK, c->pos.y * CHUNK + i / (CHUNK * CHUNK),
                                       pos.z * CHUNK + (i / CHUNK) % CHUNK});
            }
        }
    return out;
}
