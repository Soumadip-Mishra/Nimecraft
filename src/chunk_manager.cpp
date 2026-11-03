#include "chunk_manager.hpp"

#include "light.hpp"
#include "storage.hpp"
#include "terrain.hpp"

#include <algorithm>
#include <utility>

bool mesh_result_current(const World& w, ChunkPos p, uint32_t version) {
    const Chunk* c = w.chunk(p);
    return c != nullptr && c->version == version;
}

ChunkManager::ChunkManager(World& w, ThreadPool& pool, RegionStore* store, int render_distance)
    : world_(w), pool_(pool), store_(store), rd_(render_distance) {
    for (int dz = -rd_; dz <= rd_; ++dz)
        for (int dx = -rd_; dx <= rd_; ++dx)
            if (dx * dx + dz * dz <= rd_ * rd_) disc_.push_back(ColumnPos{dx, dz});
    std::stable_sort(disc_.begin(), disc_.end(), [](ColumnPos a, ColumnPos b) {
        return a.x * a.x + a.z * a.z < b.x * b.x + b.z * b.z;
    });
}

ChunkManager::~ChunkManager() {
    pool_.wait_idle();  // jobs push into this manager's queues
}

void ChunkManager::update(ColumnPos center) {
    center_ = center;
    integrate_generation();
    // Edits first: a dirty chunk gets a new job here, so an outdated result integrated next is
    // already superseded and is not rescheduled a second time.
    remesh_dirty_async();
    integrate_meshes();
    unload_far();
    mesh_ready_columns();
    schedule_generation();
}

void ChunkManager::flush_edits() {
    for (ChunkPos p : world_.take_dirty()) {
        if (!meshed_.contains(column_of(p)) || world_.chunk(p) == nullptr) continue;
        latest_.erase(p);  // any in-flight job for p is now older than this mesh
        // First in line, so a capped take_meshes shows the edit this frame even behind a backlog.
        deliver(MeshResult{p, world_.chunk(p)->version, build_mesh(snapshot(world_, p), p)}, true);
    }
}

std::vector<MeshResult> ChunkManager::take_meshes(std::size_t max) {
    std::vector<MeshResult> out;
    while (out.size() < max && !ready_order_.empty()) {
        const ChunkPos p = ready_order_.front();
        ready_order_.pop_front();
        auto it = ready_.find(p);
        if (it == ready_.end()) continue;  // removed, or already handed out under an earlier entry
        out.push_back(std::move(it->second));
        ready_.erase(it);
    }
    return out;
}

std::vector<ChunkPos> ChunkManager::take_removed() {
    std::vector<ChunkPos> out(removed_.begin(), removed_.end());
    removed_.clear();
    return out;
}

bool ChunkManager::column_ready(ColumnPos c) const {
    return world_.column_loaded(c) && world_.column_loaded(ColumnPos{c.x + 1, c.z}) &&
           world_.column_loaded(ColumnPos{c.x - 1, c.z}) &&
           world_.column_loaded(ColumnPos{c.x, c.z + 1}) &&
           world_.column_loaded(ColumnPos{c.x, c.z - 1});
}

std::size_t ChunkManager::pending_jobs() const {
    return generating_.size() + mesh_jobs_;
}

void ChunkManager::settle(ColumnPos center) {
    for (;;) {
        update(center);
        if (pending_jobs() == 0) return;  // update schedules all it can, so nothing is left
        pool_.wait_idle();
    }
}

void ChunkManager::integrate_generation() {
    while (auto r = gen_results_.try_pop()) {
        generating_.erase(r->pos);
        if (!within(r->pos, rd_ + 2)) continue;  // the player moved away while it was generated
        world_.insert_column(r->pos, std::move(r->column));
        // Each pair of neighbors is stitched when the second of the two arrives, so light across
        // their border is whole before either becomes ready to mesh; a later diagonal arrival
        // re-dirties the chunks whose light it changes.
        stitch_column_light(world_, r->pos);
        // A meshed diagonal neighbor read this column as unloaded at its corner vertices (AO and
        // smooth light): mesh its non-empty chunks again. Empty chunks have no faces either way.
        for (const ColumnPos d : {ColumnPos{r->pos.x + 1, r->pos.z + 1}, ColumnPos{r->pos.x + 1, r->pos.z - 1},
                                  ColumnPos{r->pos.x - 1, r->pos.z + 1}, ColumnPos{r->pos.x - 1, r->pos.z - 1}}) {
            if (!meshed_.contains(d)) continue;
            for (int y = 0; y < SECTIONS; ++y) {
                const ChunkPos p{d.x, y, d.z};
                if (!world_.chunk(p)->is_empty()) world_.mark_dirty(p);
            }
        }
    }
}

void ChunkManager::integrate_meshes() {
    while (auto r = mesh_results_.try_pop()) {
        --mesh_jobs_;
        const ChunkPos p = r->result.pos;
        auto it = latest_.find(p);
        if (it == latest_.end() || it->second != r->seq) continue;  // superseded or unmeshed
        latest_.erase(it);
        if (mesh_result_current(world_, p, r->result.version)) {
            deliver(std::move(r->result));
        } else if (world_.chunk(p) != nullptr) {
            schedule_mesh(p);  // edited after the snapshot: rebuild from current blocks
        }  // else the column was removed from World by someone else: nothing to mesh
    }
}

void ChunkManager::unload_far() {
    for (ColumnPos c : world_.loaded_columns())
        if (!within(c, rd_ + 2)) unload(c);
}

void ChunkManager::unload(ColumnPos c) {
    const Column col = world_.remove_column(c);
    if (store_ != nullptr &&
        std::any_of(col.begin(), col.end(), [](const auto& ch) { return ch && ch->modified; }))
        store_->save_column(c, col);
    unmesh(c);
    unmesh(ColumnPos{c.x + 1, c.z});
    unmesh(ColumnPos{c.x - 1, c.z});
    unmesh(ColumnPos{c.x, c.z + 1});
    unmesh(ColumnPos{c.x, c.z - 1});
}

void ChunkManager::unmesh(ColumnPos c) {
    meshed_.erase(c);
    for (int y = 0; y < SECTIONS; ++y) {
        const ChunkPos p{c.x, y, c.z};
        latest_.erase(p);
        push_removed(p);
    }
}

void ChunkManager::remesh_dirty_async() {
    for (ChunkPos p : world_.take_dirty())
        if (meshed_.contains(column_of(p)) && world_.chunk(p) != nullptr) schedule_mesh(p);
}

void ChunkManager::mesh_ready_columns() {
    for (ColumnPos c : world_.loaded_columns()) {
        if (meshed_.contains(c) || !column_ready(c)) continue;
        meshed_.insert(c);
        for (int y = 0; y < SECTIONS; ++y) schedule_mesh(ChunkPos{c.x, y, c.z});
    }
}

void ChunkManager::schedule_generation() {
    const std::size_t cap = 2 * static_cast<std::size_t>(pool_.size());
    const uint32_t seed = world_.seed();
    for (ColumnPos off : disc_) {
        if (generating_.size() >= cap) return;
        const ColumnPos c{center_.x + off.x, center_.z + off.z};
        if (world_.column_loaded(c) || generating_.contains(c)) continue;
        generating_.insert(c);
        RegionStore* store = store_;
        pool_.submit([this, store, seed, c] {
            // A saved column wins over the generator; RegionStore is thread-safe.
            std::optional<Column> saved = store != nullptr ? store->load_column(c) : std::nullopt;
            Column col = saved ? std::move(*saved) : generate_column(seed, c);
            compute_column_light(col);  // light is never saved: computed for every column that arrives
            gen_results_.push(GenResult{c, std::move(col)});
        });
    }
}

void ChunkManager::schedule_mesh(ChunkPos p) {
    const uint64_t seq = next_seq_++;
    latest_[p] = seq;
    ++mesh_jobs_;
    const uint32_t version = world_.chunk(p)->version;
    pool_.submit([this, pad = snapshot(world_, p), p, version, seq] {
        mesh_results_.push(MeshJobResult{MeshResult{p, version, build_mesh(pad, p)}, seq});
    });
}

void ChunkManager::deliver(MeshResult r, bool first) {
    const ChunkPos p = r.pos;
    removed_.erase(p);  // the new mesh replaces whatever the renderer holds
    // A chunk already waiting keeps one mesh (the newest); a later duplicate order entry finds it
    // gone and is skipped by take_meshes.
    if (first) ready_order_.push_front(p);
    else if (!ready_.contains(p)) ready_order_.push_back(p);
    ready_.insert_or_assign(p, std::move(r));
}

void ChunkManager::push_removed(ChunkPos p) {
    ready_.erase(p);  // a mesh not yet handed out for a removed chunk must never be uploaded
    removed_.insert(p);
}

bool ChunkManager::within(ColumnPos c, int radius) const {
    const long long dx = static_cast<long long>(c.x) - center_.x;
    const long long dz = static_cast<long long>(c.z) - center_.z;
    return dx * dx + dz * dz <= static_cast<long long>(radius) * radius;
}
