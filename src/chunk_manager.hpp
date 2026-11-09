// Streams columns around the player: schedules generation and meshing on the ThreadPool,
// integrates finished results on the main thread, and unloads far columns.
// Main thread only, except the jobs it submits, which work on copies (Global Constraints).
#pragma once

#include "coords.hpp"
#include "jobs.hpp"
#include "mesher.hpp"
#include "world.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class RegionStore;

struct MeshResult {
    ChunkPos pos;
    uint32_t version;
    ChunkMesh mesh;
};

// Finished columns inserted into World per update(); the rest wait for the next frames, so a burst
// of arrivals (inserting and light-stitching each) never lands in one frame.
constexpr std::size_t MAX_ARRIVALS_PER_UPDATE = 4;

// The chunk is loaded and its version is still `version`.
bool mesh_result_current(const World& w, ChunkPos p, uint32_t version);

class ChunkManager {
public:
    ChunkManager(World& w, ThreadPool& pool, RegionStore* store, int render_distance);
    ~ChunkManager();  // pool.wait_idle()
    ChunkManager(const ChunkManager&) = delete;
    ChunkManager& operator=(const ChunkManager&) = delete;

    void update(ColumnPos center);  // integrate finished jobs, unload far, schedule gen + mesh
    void flush_edits();             // synchronously mesh every World dirty chunk that is meshable
    std::vector<MeshResult> take_meshes(std::size_t max);  // includes empty meshes (renderer removes old)
    std::vector<ChunkPos> take_removed();                  // GPU meshes to drop
    bool column_ready(ColumnPos c) const;  // loaded and all 4 horizontal neighbors loaded
    std::size_t pending_jobs() const;
    void settle(ColumnPos center);  // test helper: update + wait until nothing pending or schedulable
    // Called on the main thread with each column as it unloads, before it is saved (a column with
    // any modified chunk is saved), so the caller can store what lives in it (creatures).
    void set_unload_hook(std::function<void(ColumnPos, Column&)> hook);
    // Changes the radius (values below 1 count as 1); the next update loads the new disc, and
    // columns beyond the new radius + 2 unload as usual.
    void set_render_distance(int rd);
    int render_distance() const;

private:
    struct GenResult {
        ColumnPos pos;
        Column column;
    };
    struct MeshJobResult {
        MeshResult result;
        uint64_t seq;  // sequence number given when the job was scheduled
    };

    void integrate_generation();
    void integrate_meshes();
    void unload_far();
    void unload(ColumnPos c);
    void unmesh(ColumnPos c);
    void remesh_dirty_async();
    void mesh_ready_columns();
    void schedule_generation();
    void schedule_mesh(ChunkPos p);
    void deliver(MeshResult r, bool first = false);  // first: ahead of every waiting mesh
    void push_removed(ChunkPos p);
    bool within(ColumnPos c, int radius) const;  // dx^2 + dz^2 <= radius^2 around center_

    World& world_;
    ThreadPool& pool_;
    RegionStore* store_;
    std::function<void(ColumnPos, Column&)> unload_hook_;
    int rd_;
    ColumnPos center_{0, 0};
    std::vector<ColumnPos> disc_;  // offsets with dx^2 + dz^2 <= rd^2, nearest first

    ConcurrentQueue<GenResult> gen_results_;
    ConcurrentQueue<MeshJobResult> mesh_results_;

    std::unordered_set<ColumnPos, ColumnPosHash> generating_;
    // Columns whose chunks are meshed; a column leaves the set when a neighbor unloads.
    std::unordered_set<ColumnPos, ColumnPosHash> meshed_;
    // The newest mesh job per chunk. Only that job's result may be delivered, so an older job that
    // finishes later (a neighbor edit leaves the chunk's own version unchanged, and versions restart
    // at 0 when a column reloads) can never overwrite a newer mesh. Unmeshing erases the entries.
    std::unordered_map<ChunkPos, uint64_t, ChunkPosHash> latest_;
    uint64_t next_seq_ = 1;
    std::size_t mesh_jobs_ = 0;  // mesh jobs submitted and not yet integrated

    // Meshes ready for take_meshes: delivery order plus the newest mesh per chunk.
    std::deque<ChunkPos> ready_order_;
    std::unordered_map<ChunkPos, MeshResult, ChunkPosHash> ready_;
    std::unordered_set<ChunkPos, ChunkPosHash> removed_;
};
