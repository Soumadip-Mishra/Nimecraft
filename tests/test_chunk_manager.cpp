// Tests for src/chunk_manager.*: streaming, meshing, unloading, and stale-result handling.
#include "block.hpp"
#include "chunk_manager.hpp"
#include "coords.hpp"
#include "jobs.hpp"
#include "storage.hpp"
#include "test.hpp"
#include "world.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <vector>

namespace {

bool has_mesh(const std::vector<MeshResult>& v, ChunkPos p) {
    return std::any_of(v.begin(), v.end(), [p](const MeshResult& r) { return r.pos == p; });
}

bool has_pos(const std::vector<ChunkPos>& v, ChunkPos p) {
    return std::find(v.begin(), v.end(), p) != v.end();
}

bool any_column_mesh(const std::vector<MeshResult>& v, ColumnPos c) {
    return std::any_of(v.begin(), v.end(),
                       [c](const MeshResult& r) { return r.pos.x == c.x && r.pos.z == c.z; });
}

// Holds a 1-thread pool busy until release(), so later jobs queue up behind it.
class Blocker {
public:
    explicit Blocker(ThreadPool& pool) : gate_(release_.get_future().share()) {
        std::shared_future<void> g = gate_;
        pool.submit([g] { g.wait(); });
    }
    void release() { release_.set_value(); }

private:
    std::promise<void> release_;
    std::shared_future<void> gate_;
};


// M3 Task 2: an opaque block placed in open sky shades every cell below it down to the ground, so
// its edit dirties those chunks too. The edit tests below cover scheduling, not light: a STONE roof
// at y 200 over z = 1 and x in {1, 2, 15, 16} puts their edit cells in shade first (sky 14 from
// the side), so an edit there leaves every other cell's light as it was and dirties only the
// chunks the block borders, as in M1. The roof's own meshes are settled and discarded.
void roof_over_edits(World& w, ChunkManager& m) {
    for (int x : {1, 2, 15, 16}) w.set(x, 200, 1, blk::STONE);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
}

}  // namespace

TEST(test_mesh_result_current) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, Column{});
    const ChunkPos p{0, 4, 0};
    const uint32_t v = w.chunk(p)->version;
    CHECK(mesh_result_current(w, p, v));
    w.set(1, 70, 1, blk::STONE);
    CHECK(!mesh_result_current(w, p, v));
    CHECK(mesh_result_current(w, p, v + 1));
    w.remove_column(ColumnPos{0, 0});
    CHECK(!mesh_result_current(w, p, v + 1));
}

TEST(test_manager_loads_radius) {
    World w(1);
    ThreadPool pool(4);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    CHECK(w.loaded_columns().size() == 13);
    for (ColumnPos c : w.loaded_columns()) CHECK(c.x * c.x + c.z * c.z <= 4);
    CHECK(m.column_ready(ColumnPos{0, 0}));
    CHECK(m.pending_jobs() == 0);
}

TEST(test_manager_meshes_ready_columns) {
    World w(1);
    ThreadPool pool(4);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    const std::vector<MeshResult> meshes = m.take_meshes(SIZE_MAX);
    CHECK(meshes.size() == 80);
    for (ColumnPos c : {ColumnPos{0, 0}, ColumnPos{1, 0}, ColumnPos{-1, 0}, ColumnPos{0, 1},
                        ColumnPos{0, -1}})
        for (int y = 0; y < SECTIONS; ++y) CHECK(has_mesh(meshes, ChunkPos{c.x, y, c.z}));
    for (const MeshResult& r : meshes) CHECK(r.version == w.chunk(r.pos)->version);
    CHECK(m.take_meshes(SIZE_MAX).empty());
}

TEST(test_manager_take_meshes_max) {
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    CHECK(m.take_meshes(0).empty());
    CHECK(m.take_meshes(16).size() == 16);
    CHECK(m.take_meshes(100).size() == 64);
    CHECK(m.take_meshes(100).empty());
}

TEST(test_manager_pending_jobs_capped) {
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.update(ColumnPos{0, 0});
    CHECK(m.pending_jobs() == 4);  // 2 * pool.size() generation jobs, nothing else yet
    m.settle(ColumnPos{0, 0});
    CHECK(m.pending_jobs() == 0);
}

TEST(test_manager_unloads_far) {
    World w(1);
    ThreadPool pool(4);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.settle(ColumnPos{10, 0});
    CHECK(!w.column_loaded(ColumnPos{0, 0}));
    const std::vector<ChunkPos> removed = m.take_removed();
    CHECK(has_pos(removed, ChunkPos{0, 5, 0}));
    CHECK(w.loaded_columns().size() == 13);
    for (ColumnPos c : w.loaded_columns())
        CHECK((c.x - 10) * (c.x - 10) + c.z * c.z <= 4);
}

TEST(test_manager_survives_external_column_removal) {
    // A column removed from World behind the manager's back while its mesh job is queued: the
    // outdated result is dropped instead of rescheduled on a missing chunk.
    World w(1);
    ThreadPool pool(1);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    Blocker blocker(pool);
    CHECK(w.set(1, 100, 1, blk::STONE));
    m.update(ColumnPos{0, 0});  // queued job for {0,6,0}
    w.remove_column(ColumnPos{0, 0});
    blocker.release();
    pool.wait_idle();
    m.update(ColumnPos{0, 0});
    CHECK(!has_mesh(m.take_meshes(SIZE_MAX), ChunkPos{0, 6, 0}));
    m.settle(ColumnPos{0, 0});  // regenerates (0,0) and meshes it again
    CHECK(has_mesh(m.take_meshes(SIZE_MAX), ChunkPos{0, 6, 0}));
}

TEST(test_manager_one_job_per_edit) {
    // An edit made while an older job for the same chunk is queued costs one new job, not two.
    World w(1);
    ThreadPool pool(1);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    roof_over_edits(w, m);
    Blocker blocker(pool);
    CHECK(w.set(1, 100, 1, blk::STONE));
    m.update(ColumnPos{0, 0});
    CHECK(m.pending_jobs() == 1);
    blocker.release();
    pool.wait_idle();
    CHECK(w.set(2, 100, 1, blk::STONE));
    m.update(ColumnPos{0, 0});  // the queued result is outdated; the edit schedules the only new job
    CHECK(m.pending_jobs() == 1);
    m.settle(ColumnPos{0, 0});
    CHECK(m.take_meshes(SIZE_MAX).size() == 1);
}

TEST(test_manager_unload_boundary) {
    // rd 2: columns at distance^2 <= (rd+2)^2 = 16 stay loaded, 17 and beyond are unloaded.
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.settle(ColumnPos{-4, 0});
    CHECK(w.column_loaded(ColumnPos{0, 0}));   // distance^2 16
    CHECK(!w.column_loaded(ColumnPos{1, 0}));  // distance^2 25
    CHECK(!w.column_loaded(ColumnPos{0, 1}));  // distance^2 17
}

TEST(test_manager_removed_cancels_pending_mesh) {
    // Meshes not yet taken for a chunk that is then removed are never handed out.
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.settle(ColumnPos{10, 0});
    const std::vector<MeshResult> meshes = m.take_meshes(SIZE_MAX);
    CHECK(!any_column_mesh(meshes, ColumnPos{0, 0}));
    CHECK(any_column_mesh(meshes, ColumnPos{10, 0}));
}

TEST(test_manager_drops_stale_generation) {
    World w(1);
    ThreadPool pool(4);
    ChunkManager m(w, pool, nullptr, 2);
    m.update(ColumnPos{0, 0});
    m.settle(ColumnPos{100, 0});
    for (ColumnPos c : w.loaded_columns()) CHECK(c.x * c.x + c.z * c.z > 16);
    CHECK(w.loaded_columns().size() == 13);
}

TEST(test_manager_flush_edits) {
    World w(1);
    ThreadPool pool(4);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    roof_over_edits(w, m);
    CHECK(w.set(1, 100, 1, blk::STONE));
    const uint32_t v = w.chunk(ChunkPos{0, 6, 0})->version;
    m.flush_edits();
    const std::vector<MeshResult> meshes = m.take_meshes(SIZE_MAX);
    CHECK(meshes.size() == 1);
    CHECK(has_mesh(meshes, ChunkPos{0, 6, 0}));
    for (const MeshResult& r : meshes) {
        CHECK(r.version == v);
        CHECK(r.mesh.opaque.vertex_count() == 24);
    }
    CHECK(m.pending_jobs() == 0);
}

TEST(test_manager_flush_edits_meshes_first) {
    // With a backlog of 80 meshes waiting, an edit's meshes are still in the next take of 16,
    // so the game shows the edit in the same frame (main uploads 16 meshes per frame).
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});  // 80 meshes waiting, none taken
    CHECK(w.set(15, 100, 1, blk::STONE));  // border: chunks {0,6,0} and {1,6,0}
    m.flush_edits();
    const std::vector<MeshResult> first = m.take_meshes(16);
    CHECK(first.size() == 16);
    CHECK(has_mesh(first, ChunkPos{0, 6, 0}));
    CHECK(has_mesh(first, ChunkPos{1, 6, 0}));
    for (const MeshResult& r : first)
        if (r.pos == ChunkPos{0, 6, 0}) CHECK(r.version == w.chunk(r.pos)->version);
    // Nothing is handed out twice: 80 chunks in total.
    CHECK(first.size() + m.take_meshes(SIZE_MAX).size() == 80);
}

TEST(test_manager_flush_edits_border_updates_both_chunks) {
    World w(1);
    ThreadPool pool(4);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    roof_over_edits(w, m);
    CHECK(w.set(15, 100, 1, blk::STONE));  // borders column (1,0)
    m.flush_edits();
    const std::vector<MeshResult> meshes = m.take_meshes(SIZE_MAX);
    CHECK(meshes.size() == 2);
    CHECK(has_mesh(meshes, ChunkPos{0, 6, 0}));
    CHECK(has_mesh(meshes, ChunkPos{1, 6, 0}));
}

TEST(test_manager_flush_edits_skips_unmeshed) {
    // (2,0) is loaded but not ready (its +X neighbor is outside the disc): no mesh for its edit.
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    CHECK(w.set(40, 100, 1, blk::STONE));
    m.flush_edits();
    CHECK(m.take_meshes(SIZE_MAX).empty());
}

TEST(test_manager_async_remesh_on_edit) {
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    roof_over_edits(w, m);
    CHECK(w.set(1, 100, 1, blk::STONE));
    const uint32_t v = w.chunk(ChunkPos{0, 6, 0})->version;
    m.settle(ColumnPos{0, 0});
    const std::vector<MeshResult> meshes = m.take_meshes(SIZE_MAX);
    CHECK(meshes.size() == 1);
    CHECK(has_mesh(meshes, ChunkPos{0, 6, 0}));
    for (const MeshResult& r : meshes) CHECK(r.version == v);
}

TEST(test_manager_reschedules_outdated_mesh) {
    // A mesh job whose chunk is edited before the result is integrated is rebuilt, not delivered.
    World w(1);
    ThreadPool pool(1);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    roof_over_edits(w, m);
    Blocker blocker(pool);
    CHECK(w.set(1, 100, 1, blk::STONE));
    m.update(ColumnPos{0, 0});  // queues a mesh job behind the blocker
    CHECK(w.set(2, 100, 1, blk::STONE));
    w.take_dirty();             // simulate the edit having been seen by no one: only the reschedule can fix it
    blocker.release();
    m.settle(ColumnPos{0, 0});
    const std::vector<MeshResult> meshes = m.take_meshes(SIZE_MAX);
    CHECK(meshes.size() == 1);
    for (const MeshResult& r : meshes) {
        CHECK(r.pos == (ChunkPos{0, 6, 0}));
        CHECK(r.version == w.chunk(ChunkPos{0, 6, 0})->version);
        CHECK(r.mesh.opaque.vertex_count() == 40);
    }
}

TEST(test_manager_older_mesh_job_never_wins) {
    // Job A meshes {0,6,0} with STONE at x = 15 and AIR at x = 16 (24 vertices). Then x = 16 in
    // the neighbor chunk turns STONE: {0,6,0} is dirty with its version unchanged, and job B
    // culls the +X face (20 vertices). A finishes first and is still "current" by version, but
    // only the newest job for a chunk may deliver.
    World w(1);
    ThreadPool pool(1);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    roof_over_edits(w, m);
    Blocker blocker(pool);
    CHECK(w.set(15, 100, 1, blk::STONE));
    m.update(ColumnPos{0, 0});  // job A
    CHECK(w.set(16, 100, 1, blk::STONE));
    m.update(ColumnPos{0, 0});  // job B for {0,6,0}, and one for {1,6,0}
    blocker.release();
    m.settle(ColumnPos{0, 0});
    const std::vector<MeshResult> meshes = m.take_meshes(SIZE_MAX);
    CHECK(meshes.size() == 2);
    for (const MeshResult& r : meshes) {
        if (r.pos == ChunkPos{0, 6, 0}) CHECK(r.mesh.opaque.vertex_count() == 20);
        else CHECK(r.pos == (ChunkPos{1, 6, 0}));
    }
}

TEST(test_manager_drops_mesh_of_unmeshed_column) {
    // A mesh job still queued when its column loses a neighbor (and so its meshed state) is
    // dropped even though the chunk itself is unchanged.
    World w(1);
    ThreadPool pool(1);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    m.take_removed();
    Blocker blocker(pool);
    CHECK(w.set(1, 100, 1, blk::STONE));
    m.update(ColumnPos{0, 0});   // async mesh job for {0,6,0}, queued
    m.update(ColumnPos{-4, 0});  // unloads (1,0): (0,0) is no longer ready
    CHECK(w.column_loaded(ColumnPos{0, 0}));
    CHECK(!m.column_ready(ColumnPos{0, 0}));
    blocker.release();
    pool.wait_idle();
    m.update(ColumnPos{-4, 0});
    CHECK(!has_mesh(m.take_meshes(SIZE_MAX), ChunkPos{0, 6, 0}));
    CHECK(has_pos(m.take_removed(), ChunkPos{0, 6, 0}));
}

TEST(test_manager_remeshes_after_neighbor_reload) {
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    m.take_removed();
    m.settle(ColumnPos{-4, 0});  // (1,0) unloads, (0,0) stays loaded but unmeshed
    CHECK(has_pos(m.take_removed(), ChunkPos{0, 3, 0}));
    m.take_meshes(SIZE_MAX);
    m.settle(ColumnPos{0, 0});
    const std::vector<MeshResult> meshes = m.take_meshes(SIZE_MAX);
    for (int y = 0; y < SECTIONS; ++y) CHECK(has_mesh(meshes, ChunkPos{0, y, 0}));
    // The fresh meshes win over the earlier removal: (0,0) is not removed again.
    const std::vector<ChunkPos> removed = m.take_removed();
    for (int y = 0; y < SECTIONS; ++y) CHECK(!has_pos(removed, ChunkPos{0, y, 0}));
}

TEST(test_manager_no_data_race) {
    World w(7);
    ThreadPool pool(4);
    ChunkManager m(w, pool, nullptr, 4);
    m.settle(ColumnPos{0, 0});
    w.set(3, 120, 3, blk::PLANKS);
    m.settle(ColumnPos{6, 0});
    w.set(96, 120, 3, blk::PLANKS);
    m.flush_edits();
    m.settle(ColumnPos{6, 6});
    w.set(96, 120, 96, blk::STONE);
    m.settle(ColumnPos{0, 0});
    CHECK(w.loaded_columns().size() >= 49);
    CHECK(m.pending_jobs() == 0);
}

TEST(test_manager_radius_4_count) {
    World w(1);
    ThreadPool pool(4);
    ChunkManager m(w, pool, nullptr, 4);
    m.settle(ColumnPos{0, 0});
    CHECK(w.loaded_columns().size() == 49);
}

TEST(test_manager_settle_idempotent) {
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{0, 0});
    m.take_meshes(SIZE_MAX);
    m.take_removed();
    m.settle(ColumnPos{0, 0});
    CHECK(m.take_meshes(SIZE_MAX).empty());
    CHECK(m.take_removed().empty());
    CHECK(w.loaded_columns().size() == 13);
}

TEST(test_manager_negative_center) {
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{-100, -100});
    CHECK(w.loaded_columns().size() == 13);
    for (ColumnPos c : w.loaded_columns())
        CHECK((c.x + 100) * (c.x + 100) + (c.z + 100) * (c.z + 100) <= 4);
    CHECK(m.column_ready(ColumnPos{-100, -100}));
}

TEST(test_manager_far_center) {
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    m.settle(ColumnPos{62500, -62500});  // block x = 1,000,000
    CHECK(w.loaded_columns().size() == 13);
    CHECK(m.column_ready(ColumnPos{62500, -62500}));
    CHECK(m.take_meshes(SIZE_MAX).size() == 80);
}

TEST(test_manager_column_ready_needs_neighbors) {
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 2);
    CHECK(!m.column_ready(ColumnPos{0, 0}));  // nothing loaded yet
    m.settle(ColumnPos{0, 0});
    CHECK(!m.column_ready(ColumnPos{2, 0}));
    CHECK(m.column_ready(ColumnPos{1, 0}));
    CHECK(!m.column_ready(ColumnPos{1, 1}));
    CHECK(!m.column_ready(ColumnPos{5, 5}));
}

TEST(test_manager_destructor_waits_for_jobs) {
    World w(1);
    ThreadPool pool(2);
    {
        ChunkManager m(w, pool, nullptr, 3);
        m.update(ColumnPos{0, 0});  // jobs in flight when the manager is destroyed
    }
    pool.wait_idle();
    CHECK(w.loaded_columns().empty());  // results were never integrated
}

// ---- Persistence through RegionStore (M1 Task 7) ----

TEST(test_manager_persists_edits) {
    test::TempDir tmp;
    RegionStore store(tmp.path());
    {
        World w(1);
        ThreadPool pool(2);
        ChunkManager m(w, pool, &store, 2);
        m.settle(ColumnPos{0, 0});
        CHECK(w.set(1, 100, 1, blk::PLANKS));
        m.settle(ColumnPos{20, 0});  // (0,0) unloads and is saved
        CHECK(!w.column_loaded(ColumnPos{0, 0}));
        CHECK(store.flush());
    }
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, &store, 2);
    m.settle(ColumnPos{0, 0});
    CHECK(w.get(1, 100, 1) == blk::PLANKS);
}

TEST(test_manager_persists_edits_across_stores) {
    test::TempDir tmp;
    {
        RegionStore store(tmp.path());
        World w(1);
        ThreadPool pool(2);
        ChunkManager m(w, pool, &store, 2);
        m.settle(ColumnPos{0, 0});
        CHECK(w.set(-1, 90, -1, blk::LOG));
        m.settle(ColumnPos{20, 0});
        CHECK(store.flush());
    }
    RegionStore store(tmp.path());
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, &store, 2);
    m.settle(ColumnPos{0, 0});
    CHECK(w.get(-1, 90, -1) == blk::LOG);
    CHECK(!store.load_column(ColumnPos{0, 0}).has_value());  // untouched columns are not saved
}

TEST(test_manager_unmodified_columns_not_saved) {
    test::TempDir tmp;
    RegionStore store(tmp.path());
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, &store, 2);
    m.settle(ColumnPos{0, 0});
    m.settle(ColumnPos{20, 0});
    CHECK(store.flush());
    std::size_t files = 0;
    for (const auto& e : std::filesystem::directory_iterator(tmp.path())) {
        (void)e;
        ++files;
    }
    CHECK(files == 0);
}

// M3 Task 3: a column meshes once its 4 side neighbors are loaded, but its corner vertices also
// read the diagonal columns. When a diagonal arrives later, the meshed column's non-empty chunks
// are meshed again; its empty chunks (no faces either way) are not.
TEST(test_manager_remeshes_on_diagonal_arrival) {
    World w(1);
    ThreadPool pool(2);
    ChunkManager m(w, pool, nullptr, 1);
    m.settle(ColumnPos{0, 0});  // (0,0) and its 4 side neighbors; diagonal (1,1) is outside rd 1
    CHECK(!w.column_loaded(ColumnPos{1, 1}));
    m.take_meshes(SIZE_MAX);
    m.settle(ColumnPos{1, 0});  // loads (1,1) and (1,-1), diagonals of (0,0)
    CHECK(w.column_loaded(ColumnPos{1, 1}));
    const std::vector<MeshResult> meshes = m.take_meshes(SIZE_MAX);
    CHECK(has_mesh(meshes, ChunkPos{0, 0, 0}));    // DEEPSTONE: never empty
    CHECK(!has_mesh(meshes, ChunkPos{0, 15, 0}));  // empty sky chunk
    for (const MeshResult& r : meshes)
        if (r.pos.x == 0 && r.pos.z == 0) CHECK(!w.chunk(r.pos)->is_empty());
}

TEST(test_manager_unload_hook) {
    // The hook sees each unloading column before it is saved; what it stores there is persisted.
    test::TempDir tmp;
    RegionStore store(tmp.path());
    World w(1);
    ThreadPool pool(2);
    std::vector<ColumnPos> seen;
    {
        ChunkManager m(w, pool, &store, 2);
        m.set_unload_hook([&seen](ColumnPos c, Column& col) {
            seen.push_back(c);
            if (c == ColumnPos{0, 0}) {
                col[4]->mobs.push_back(SavedMob{0, 3.5f, 70.f, 4.5f, 0.f, 10.f});
                col[4]->modified = true;
            }
        });
        m.settle(ColumnPos{0, 0});
        CHECK(seen.empty());
        m.settle(ColumnPos{10, 0});
    }
    CHECK(std::find(seen.begin(), seen.end(), ColumnPos{0, 0}) != seen.end());
    CHECK(std::find(seen.begin(), seen.end(), ColumnPos{10, 0}) == seen.end());  // still loaded
    const std::optional<Column> col = store.load_column(ColumnPos{0, 0});
    CHECK(col.has_value() && (*col)[4]->mobs.size() == 1);
    CHECK(!store.load_column(ColumnPos{1, 0}).has_value());  // untouched columns stay unsaved
}
