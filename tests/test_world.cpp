// Tests for src/chunk.* and src/world.*: block storage, world-space get/set, columns, dirty set.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "test.hpp"
#include "world.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace {

Column air_column(ColumnPos c) {
    Column col;
    for (std::size_t i = 0; i < col.size(); ++i) {
        col[i] = std::make_unique<Chunk>();
        col[i]->pos = ChunkPos{c.x, static_cast<int>(i), c.z};
    }
    return col;
}

bool has(const std::vector<ChunkPos>& v, ChunkPos p) {
    return std::find(v.begin(), v.end(), p) != v.end();
}

// True when v holds exactly the (distinct) positions in want, in any order, without duplicates.
bool same_set(const std::vector<ChunkPos>& v, std::initializer_list<ChunkPos> want) {
    if (v.size() != want.size()) return false;
    for (ChunkPos p : want)
        if (!has(v, p)) return false;
    return true;
}

}  // namespace

// ---- Chunk ----

TEST(test_chunk_index_layout) {
    CHECK(Chunk::index(0, 0, 0) == 0);
    CHECK(Chunk::index(1, 0, 0) == 1);
    CHECK(Chunk::index(15, 0, 0) == 15);
    CHECK(Chunk::index(0, 0, 1) == 16);
    CHECK(Chunk::index(0, 1, 0) == 256);
    CHECK(Chunk::index(3, 2, 5) == 3 + 5 * 16 + 2 * 256);
    CHECK(Chunk::index(15, 15, 15) == 4095);
}

TEST(test_chunk_set_get_version) {
    Chunk c;
    CHECK(c.version == 0);
    CHECK(!c.modified);
    CHECK(c.get(0, 0, 0) == blk::AIR);
    c.set(3, 2, 5, blk::SAND);
    CHECK(c.get(3, 2, 5) == blk::SAND);
    CHECK(c.blocks[3 + 5 * 16 + 2 * 256] == blk::SAND);
    CHECK(c.get(5, 2, 3) == blk::AIR);
    CHECK(c.version == 1);
    c.set(3, 2, 5, blk::SAND);  // every set() counts, even with the same block
    CHECK(c.version == 2);
    c.set(15, 15, 15, blk::LOG);
    CHECK(c.get(15, 15, 15) == blk::LOG);
    CHECK(c.version == 3);
    CHECK(!c.modified);  // only World::set marks modified
}

TEST(test_chunk_is_empty) {
    Chunk c;
    CHECK(c.is_empty());
    c.set(15, 15, 15, blk::STONE);  // last cell
    CHECK(!c.is_empty());
    c.set(15, 15, 15, blk::AIR);
    CHECK(c.is_empty());
    c.set(0, 0, 0, blk::DIRT);  // first cell
    CHECK(!c.is_empty());
}

TEST(test_chunk_full) {
    Chunk c;
    for (int y = 0; y < CHUNK; ++y)
        for (int z = 0; z < CHUNK; ++z)
            for (int x = 0; x < CHUNK; ++x) c.set(x, y, z, blk::STONE);
    CHECK(!c.is_empty());
    CHECK(c.version == 4096);
    CHECK(std::all_of(c.blocks.begin(), c.blocks.end(), [](BlockId b) { return b == blk::STONE; }));
}

// ---- World ----

TEST(test_world_seed) {
    CHECK(World(42).seed() == 42);
    CHECK(World(0xFFFFFFFFu).seed() == 0xFFFFFFFFu);
}

TEST(test_world_unloaded_and_bounds) {
    World w(1);
    CHECK(w.get(0, 10, 0) == blk::AIR);
    CHECK(w.get(0, -1, 0) == blk::DEEPSTONE);
    CHECK(w.get(0, 256, 0) == blk::AIR);
    CHECK(w.set(0, 10, 0, blk::STONE) == false);
    CHECK(w.take_dirty().empty());
}

TEST(test_world_empty) {
    const World w(1);
    CHECK(w.loaded_columns().empty());
    CHECK(!w.column_loaded(ColumnPos{0, 0}));
    CHECK(w.chunk(ChunkPos{0, 0, 0}) == nullptr);
}

TEST(test_world_set_get_negative) {
    World w(1);
    w.insert_column(ColumnPos{-1, -1}, air_column(ColumnPos{-1, -1}));
    CHECK(w.set(-1, 70, -1, blk::PLANKS));
    CHECK(w.get(-1, 70, -1) == blk::PLANKS);
    Chunk* c = w.chunk(ChunkPos{-1, 4, -1});
    CHECK(c != nullptr);
    if (c == nullptr) return;
    CHECK(c->get(15, 6, 15) == blk::PLANKS);
    CHECK(c->modified);
    CHECK(c->version == 1);
    CHECK(w.get(0, 70, 0) == blk::AIR);  // neighbor column is not loaded
}

TEST(test_world_set_marks_neighbor_dirty) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    w.insert_column(ColumnPos{1, 0}, air_column(ColumnPos{1, 0}));
    w.take_dirty();

    CHECK(w.set(16, 32, 5, blk::STONE));
    std::vector<ChunkPos> d = w.take_dirty();
    CHECK(has(d, ChunkPos{1, 2, 0}));
    CHECK(has(d, ChunkPos{0, 2, 0}));
    CHECK(!has(d, ChunkPos{2, 2, 0}));  // wrong-side marking: also see the all-six test below

    CHECK(w.set(17, 33, 5, blk::STONE));
    CHECK(same_set(w.take_dirty(), {ChunkPos{1, 2, 0}}));

    CHECK(w.set(17, 32, 5, blk::STONE));
    CHECK(same_set(w.take_dirty(), {ChunkPos{1, 2, 0}, ChunkPos{1, 1, 0}}));
}

// Both corners of chunk {0,1,0} with all four horizontal neighbor columns loaded.
TEST(test_world_set_marks_all_six_neighbors) {
    World w(1);
    for (ColumnPos c : {ColumnPos{0, 0}, ColumnPos{-1, 0}, ColumnPos{1, 0}, ColumnPos{0, -1}, ColumnPos{0, 1}})
        w.insert_column(c, air_column(c));
    w.take_dirty();

    // M3 Task 3: smooth light and AO read edge and corner cells of the padded snapshot, so every
    // loaded chunk whose 18^3 snapshot holds the cell is dirty, diagonals included. Here the
    // diagonal columns (-1,-1), (1,1), ... are unloaded, so the X-Z diagonals stay out.
    CHECK(w.set(0, 16, 0, blk::DIRT));  // local (0,0,0): -X, -Y, -Z
    CHECK(same_set(w.take_dirty(), {ChunkPos{0, 1, 0}, ChunkPos{-1, 1, 0}, ChunkPos{0, 0, 0}, ChunkPos{0, 1, -1},
                                    ChunkPos{-1, 0, 0}, ChunkPos{0, 0, -1}}));
    // Neighbors only need a new mesh; their blocks did not change, so they are not modified.
    const Chunk* nb = w.chunk(ChunkPos{-1, 1, 0});
    CHECK(nb != nullptr);
    if (nb != nullptr) {
        CHECK(!nb->modified);
        CHECK(nb->version == 0);
    }

    CHECK(w.set(15, 31, 15, blk::DIRT));  // local (15,15,15): +X, +Y, +Z
    CHECK(same_set(w.take_dirty(), {ChunkPos{0, 1, 0}, ChunkPos{1, 1, 0}, ChunkPos{0, 2, 0}, ChunkPos{0, 1, 1},
                                    ChunkPos{1, 2, 0}, ChunkPos{0, 2, 1}}));

    CHECK(w.set(8, 24, 8, blk::DIRT));  // interior: own chunk only
    CHECK(same_set(w.take_dirty(), {ChunkPos{0, 1, 0}}));
}

// An edit next to an unloaded column succeeds and marks only loaded chunks.
TEST(test_world_set_next_to_unloaded_column) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    w.take_dirty();
    CHECK(w.set(0, 40, 15, blk::STONE));  // borders unloaded columns (-1,0) and (0,1)
    CHECK(w.get(0, 40, 15) == blk::STONE);
    CHECK(same_set(w.take_dirty(), {ChunkPos{0, 2, 0}}));
    CHECK(w.get(-1, 40, 15) == blk::AIR);
    CHECK(w.get(0, 40, 16) == blk::AIR);
}

TEST(test_world_remove_column) {
    World w(1);
    w.insert_column(ColumnPos{3, 4}, air_column(ColumnPos{3, 4}));
    CHECK(w.column_loaded(ColumnPos{3, 4}));
    Column col = w.remove_column(ColumnPos{3, 4});
    CHECK(!w.column_loaded(ColumnPos{3, 4}));
    int non_null = 0;
    for (const auto& c : col)
        if (c) ++non_null;
    CHECK(non_null == 16);
    CHECK(w.chunk(ChunkPos{3, 0, 4}) == nullptr);
    CHECK(w.loaded_columns().empty());
}

TEST(test_world_remove_absent_column) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    Column col = w.remove_column(ColumnPos{5, 5});
    for (const auto& c : col) CHECK(c == nullptr);
    CHECK(w.column_loaded(ColumnPos{0, 0}));
}

// A removed column leaves nothing behind in the dirty set.
TEST(test_world_remove_column_drops_dirty) {
    World w(1);
    w.insert_column(ColumnPos{3, 4}, air_column(ColumnPos{3, 4}));
    w.insert_column(ColumnPos{4, 4}, air_column(ColumnPos{4, 4}));
    w.take_dirty();
    CHECK(w.set(64, 5, 64, blk::STONE));  // column (4,4), local x 0: borders (3,4)
    w.remove_column(ColumnPos{4, 4});
    CHECK(same_set(w.take_dirty(), {ChunkPos{3, 0, 4}}));
}

TEST(test_world_insert_marks_dirty) {
    World w(1);
    w.insert_column(ColumnPos{2, -3}, air_column(ColumnPos{2, -3}));
    std::vector<ChunkPos> d = w.take_dirty();
    CHECK(d.size() == 16);
    for (int y = 0; y < 16; ++y) CHECK(has(d, ChunkPos{2, y, -3}));
    CHECK(w.take_dirty().empty());
}

TEST(test_world_insert_replaces) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    CHECK(w.set(1, 1, 1, blk::SAND));
    w.take_dirty();
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    CHECK(w.get(1, 1, 1) == blk::AIR);
    CHECK(w.loaded_columns().size() == 1);
    CHECK(w.take_dirty().size() == 16);
}

// Missing chunks are filled with AIR and every chunk gets its own position.
TEST(test_world_insert_fills_missing_chunks) {
    World w(1);
    Column col;
    col[3] = std::make_unique<Chunk>();  // wrong pos {0,0,0}; every other slot null
    col[3]->set(0, 0, 0, blk::LOG);
    w.insert_column(ColumnPos{-2, 7}, std::move(col));
    for (int y = 0; y < 16; ++y) {
        const Chunk* c = w.chunk(ChunkPos{-2, y, 7});
        CHECK(c != nullptr);
        if (c != nullptr) CHECK(c->pos == (ChunkPos{-2, y, 7}));
    }
    CHECK(w.get(-32, 48, 112) == blk::LOG);
    CHECK(w.set(-32, 0, 112, blk::STONE));
    CHECK(w.get(-32, 0, 112) == blk::STONE);
}

TEST(test_world_loaded_columns) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    w.insert_column(ColumnPos{-1, 5}, air_column(ColumnPos{-1, 5}));
    w.insert_column(ColumnPos{7, -2}, air_column(ColumnPos{7, -2}));
    std::vector<ColumnPos> cols = w.loaded_columns();
    CHECK(cols.size() == 3);
    for (ColumnPos c : {ColumnPos{0, 0}, ColumnPos{-1, 5}, ColumnPos{7, -2}})
        CHECK(std::find(cols.begin(), cols.end(), c) != cols.end());
    CHECK(w.column_loaded(ColumnPos{-1, 5}));
    CHECK(!w.column_loaded(ColumnPos{5, -1}));
}

TEST(test_world_chunk_lookup) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    CHECK(w.chunk(ChunkPos{0, 0, 0}) != nullptr);
    CHECK(w.chunk(ChunkPos{0, 15, 0}) != nullptr);
    CHECK(w.chunk(ChunkPos{0, -1, 0}) == nullptr);
    CHECK(w.chunk(ChunkPos{0, 16, 0}) == nullptr);
    CHECK(w.chunk(ChunkPos{1, 0, 0}) == nullptr);
    const World& cw = w;
    CHECK(cw.chunk(ChunkPos{0, 7, 0}) == w.chunk(ChunkPos{0, 7, 0}));
    CHECK(cw.chunk(ChunkPos{0, 7, 0}) != nullptr);
    CHECK(cw.chunk(ChunkPos{0, 16, 0}) == nullptr);
    CHECK(cw.chunk(ChunkPos{0, -1, 0}) == nullptr);
    CHECK(cw.chunk(ChunkPos{0, 0, 1}) == nullptr);
}

TEST(test_world_height_edges) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    w.take_dirty();

    CHECK(w.set(0, 255, 0, blk::STONE));
    CHECK(w.get(0, 255, 0) == blk::STONE);
    std::vector<ChunkPos> d = w.take_dirty();
    CHECK(has(d, ChunkPos{0, 15, 0}));
    for (ChunkPos p : d) CHECK(p.y != 16);

    CHECK(!w.set(0, 256, 0, blk::STONE));
    CHECK(!w.set(0, -1, 0, blk::STONE));
    CHECK(w.get(0, 256, 0) == blk::AIR);
    CHECK(w.get(0, -1, 0) == blk::DEEPSTONE);
    CHECK(w.take_dirty().empty());
    const Chunk* top = w.chunk(ChunkPos{0, 15, 0});
    const Chunk* bottom = w.chunk(ChunkPos{0, 0, 0});
    CHECK(top != nullptr && bottom != nullptr);
    if (top == nullptr || bottom == nullptr) return;
    CHECK(top->version == 1);
    CHECK(bottom->version == 0);
}

// y = 0 is the lowest editable block; its chunk has no neighbor below.
TEST(test_world_bottom_edge) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    w.take_dirty();
    CHECK(w.set(5, 0, 5, blk::STONE));
    CHECK(w.get(5, 0, 5) == blk::STONE);
    CHECK(same_set(w.take_dirty(), {ChunkPos{0, 0, 0}}));
    CHECK(w.get(5, -1000000, 5) == blk::DEEPSTONE);
    CHECK(w.get(5, 1000000, 5) == blk::AIR);
}

TEST(test_world_set_same_block_noop) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    CHECK(w.set(4, 20, 4, blk::SAND));
    w.take_dirty();
    const Chunk* c = w.chunk(ChunkPos{0, 1, 0});
    CHECK(c != nullptr);
    if (c == nullptr) return;
    CHECK(c->version == 1);

    CHECK(w.set(4, 20, 4, blk::SAND));
    CHECK(c->version == 1);
    CHECK(w.take_dirty().empty());

    CHECK(w.set(9, 25, 9, blk::AIR));  // AIR over AIR in a loaded column
    CHECK(c->version == 1);
    CHECK(w.take_dirty().empty());
}

// A no-op set on a freshly loaded chunk does not mark it modified.
TEST(test_world_set_same_block_keeps_unmodified) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    CHECK(w.set(1, 1, 1, blk::AIR));
    const Chunk* c = w.chunk(ChunkPos{0, 0, 0});
    CHECK(c != nullptr);
    if (c != nullptr) CHECK(!c->modified);
}

// Several edits to one chunk between two take_dirty() calls report that chunk once.
TEST(test_world_take_dirty_no_duplicates) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, air_column(ColumnPos{0, 0}));
    w.take_dirty();
    CHECK(w.set(1, 1, 1, blk::STONE));
    CHECK(w.set(2, 2, 2, blk::STONE));
    CHECK(w.set(3, 3, 3, blk::STONE));
    CHECK(same_set(w.take_dirty(), {ChunkPos{0, 0, 0}}));
    CHECK(w.take_dirty().empty());
}

TEST(test_world_far_coordinates) {
    World w(1);
    w.insert_column(ColumnPos{62500, -62500}, air_column(ColumnPos{62500, -62500}));
    CHECK(w.set(1000000, 100, -1000000, blk::SAND));
    CHECK(w.get(1000000, 100, -1000000) == blk::SAND);
    const Chunk* c = w.chunk(ChunkPos{62500, 6, -62500});
    CHECK(c != nullptr);
    if (c != nullptr) CHECK(c->get(0, 4, 0) == blk::SAND);
    CHECK(w.get(999999, 100, -1000000) == blk::AIR);  // column 62499 is not loaded
}

// M3 Task 3: an edit at a chunk corner dirties all 8 chunks around the corner when they are
// loaded (AO and smooth light read diagonal cells); an edge cell up to 4.
TEST(test_world_set_marks_diagonal_neighbors) {
    World w(1);
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz) w.insert_column(ColumnPos{cx, cz}, air_column(ColumnPos{cx, cz}));
    w.take_dirty();
    CHECK(w.set(15, 31, 15, blk::DIRT));
    CHECK(same_set(w.take_dirty(), {ChunkPos{0, 1, 0}, ChunkPos{1, 1, 0}, ChunkPos{0, 2, 0}, ChunkPos{0, 1, 1},
                                    ChunkPos{1, 2, 0}, ChunkPos{0, 2, 1}, ChunkPos{1, 1, 1}, ChunkPos{1, 2, 1}}));
    // Local (0, 8, 0) of chunk {-1,1,0}: the -X, -Z edge. Column (-2, *) is unloaded, so only
    // the -Z neighbor joins.
    CHECK(w.set(-16, 24, 0, blk::DIRT));
    CHECK(same_set(w.take_dirty(), {ChunkPos{-1, 1, 0}, ChunkPos{-1, 1, -1}}));
}
