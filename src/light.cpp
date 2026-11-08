#include "light.hpp"

#include "world.hpp"

#include <algorithm>
#include <cstddef>
#include <tuple>
#include <vector>

namespace {

constexpr int MAX_LIGHT = 15;
constexpr int DOWN = 3;  // index of (0, -1, 0) in DIRS
constexpr int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

enum class Channel { Sky, Block };

struct Node {
    int x, y, z;
    int l;  // removal: the light the cell had before it was cleared
};

// A cell found by a Cells accessor.
struct Ref {
    Chunk* chunk = nullptr;
    int i = 0;
};

int get(Channel ch, const Ref& r) {
    return ch == Channel::Sky ? sky_light(*r.chunk, r.i) : block_light(*r.chunk, r.i);
}

void put(Channel ch, const Ref& r, int v) {
    if (ch == Channel::Sky) set_sky_light(*r.chunk, r.i, static_cast<uint8_t>(v));
    else set_block_light(*r.chunk, r.i, static_cast<uint8_t>(v));
}

const BlockDef& def_at(const Ref& r) {
    return block_def(r.chunk->blocks[static_cast<std::size_t>(r.i)]);
}

// Light a cell of level l sends to its neighbor in direction dir whose opacity is op.
int sent(Channel ch, int l, int dir, int op) {
    if (ch == Channel::Sky && dir == DOWN) return std::max(0, l - op);
    return std::max(0, l - 1 - op);
}

// The light a cell has on its own: emission for block light, the sky above the world for y = 255.
int source(Channel ch, const BlockDef& d, int y) {
    if (ch == Channel::Block) return d.emission;
    return y == WORLD_HEIGHT - 1 ? std::max(0, MAX_LIGHT - d.opacity) : 0;
}

// The cells of one column, local x and z in [0, CHUNK); light never leaves it.
struct ColumnCells {
    Column& col;
    bool find(int x, int y, int z, Ref& r) const {
        if (x < 0 || x >= CHUNK || z < 0 || z >= CHUNK || y < 0 || y >= WORLD_HEIGHT) return false;
        r = Ref{col[static_cast<std::size_t>(y / CHUNK)].get(), Chunk::index(x, y % CHUNK, z)};
        return true;
    }
    void write(Channel ch, const Ref& r, int, int, int, int v) const { put(ch, r, v); }
};

// A cell's light before an update wrote it.
struct Change {
    int x, y, z;
    int old;
};

// The cells of a World's loaded columns, in world coordinates. Every write is logged so that
// mark_changed_dirty can mark exactly the chunks whose light ends up different.
struct WorldCells {
    World& w;
    std::vector<Change>& log;
    bool find(int x, int y, int z, Ref& r) const {
        if (y < 0 || y >= WORLD_HEIGHT) return false;
        Chunk* c = w.chunk(chunk_of(x, y, z));
        if (c == nullptr) return false;
        r = Ref{c, Chunk::index(floor_mod(x, CHUNK), floor_mod(y, CHUNK), floor_mod(z, CHUNK))};
        return true;
    }
    void write(Channel ch, const Ref& r, int x, int y, int z, int v) const {
        log.push_back(Change{x, y, z, get(ch, r)});
        put(ch, r, v);
    }
};

// Marks dirty every chunk whose padded snapshot (the chunk plus 1 cell around) holds a logged
// cell whose light now differs from its value before the first logged write.
void mark_changed_dirty(const WorldCells& cells, Channel ch, std::vector<Change>& log) {
    std::stable_sort(log.begin(), log.end(), [](const Change& a, const Change& b) {
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    });
    for (std::size_t i = 0; i < log.size(); ++i) {
        const Change& c = log[i];
        if (i > 0 && log[i - 1].x == c.x && log[i - 1].y == c.y && log[i - 1].z == c.z) continue;
        Ref r;
        cells.find(c.x, c.y, c.z, r);  // logged cells are loaded
        if (get(ch, r) == c.old) continue;
        const ChunkPos p = chunk_of(c.x, c.y, c.z);
        auto range = [](int local, int& lo, int& hi) {
            lo = local == 0 ? -1 : 0;
            hi = local == CHUNK - 1 ? 1 : 0;
        };
        int x0, x1, y0, y1, z0, z1;
        range(floor_mod(c.x, CHUNK), x0, x1);
        range(floor_mod(c.y, CHUNK), y0, y1);
        range(floor_mod(c.z, CHUNK), z0, z1);
        for (int dy = y0; dy <= y1; ++dy)
            for (int dz = z0; dz <= z1; ++dz)
                for (int dx = x0; dx <= x1; ++dx) cells.w.mark_dirty(ChunkPos{p.x + dx, p.y + dy, p.z + dz});
    }
    log.clear();
}

// Add BFS: each queued cell sends its current light to its neighbors; a neighbor that rises is
// queued in turn.
template <class Cells>
void spread(const Cells& cells, Channel ch, std::vector<Node>& queue) {
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const Node n = queue[head];
        Ref r;
        if (!cells.find(n.x, n.y, n.z, r)) continue;
        const int l = get(ch, r);
        if (l == 0) continue;
        for (int d = 0; d < 6; ++d) {
            const int nx = n.x + DIRS[d][0], ny = n.y + DIRS[d][1], nz = n.z + DIRS[d][2];
            Ref nr;
            if (!cells.find(nx, ny, nz, nr)) continue;
            const int v = sent(ch, l, d, def_at(nr).opacity);
            if (v <= get(ch, nr)) continue;
            cells.write(ch, nr, nx, ny, nz, v);
            queue.push_back(Node{nx, ny, nz, 0});
        }
    }
}

// Removal BFS: each queued cell was cleared from light n.l. A neighbor whose light it could have
// sent (at most what it sends) is cleared and queued; a brighter neighbor has another source and is
// queued in `add` to spread back. A cleared cell keeps its own source light and is queued in `add`.
template <class Cells>
void unspread(const Cells& cells, Channel ch, std::vector<Node>& removal, std::vector<Node>& add) {
    for (std::size_t head = 0; head < removal.size(); ++head) {
        const Node n = removal[head];
        for (int d = 0; d < 6; ++d) {
            const int nx = n.x + DIRS[d][0], ny = n.y + DIRS[d][1], nz = n.z + DIRS[d][2];
            Ref nr;
            if (!cells.find(nx, ny, nz, nr)) continue;
            const int nl = get(ch, nr);
            if (nl == 0) continue;
            const BlockDef& nd = def_at(nr);
            if (nl > sent(ch, n.l, d, nd.opacity)) {
                add.push_back(Node{nx, ny, nz, 0});
                continue;
            }
            const int own = source(ch, nd, ny);
            cells.write(ch, nr, nx, ny, nz, own);
            removal.push_back(Node{nx, ny, nz, nl});
            if (own > 0) add.push_back(Node{nx, ny, nz, 0});
        }
    }
}

void relight_channel(const WorldCells& cells, Channel ch, int x, int y, int z) {
    Ref r;
    if (!cells.find(x, y, z, r)) return;
    std::vector<Node> removal, add;
    const int old = get(ch, r);
    if (old > 0) {
        cells.write(ch, r, x, y, z, 0);
        removal.push_back(Node{x, y, z, old});
        unspread(cells, ch, removal, add);
    }
    const int own = source(ch, def_at(r), y);
    if (own > get(ch, r)) cells.write(ch, r, x, y, z, own);
    add.push_back(Node{x, y, z, 0});
    for (const auto& d : DIRS) add.push_back(Node{x + d[0], y + d[1], z + d[2], 0});  // light flowing in
    spread(cells, ch, add);
    mark_changed_dirty(cells, ch, cells.log);
}

}  // namespace

void compute_column_light(Column& col) {
    for (auto& ch : col) ch->light.fill(0);
    const ColumnCells cells{col};
    Ref r;

    // Sky: straight down from the top, then sideways wherever a neighbor would rise.
    for (int z = 0; z < CHUNK; ++z)
        for (int x = 0; x < CHUNK; ++x) {
            int l = MAX_LIGHT;
            for (int y = WORLD_HEIGHT - 1; y >= 0 && l > 0; --y) {
                cells.find(x, y, z, r);
                l = std::max(0, l - def_at(r).opacity);
                put(Channel::Sky, r, l);
            }
        }
    std::vector<Node> queue;
    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int z = 0; z < CHUNK; ++z)
            for (int x = 0; x < CHUNK; ++x) {
                cells.find(x, y, z, r);
                const int l = get(Channel::Sky, r);
                if (l < 2) continue;
                bool rises = false;
                for (int d = 0; d < 6 && !rises; ++d) {
                    if (DIRS[d][1] != 0) continue;  // up and down are settled by the pass above
                    Ref nr;
                    if (!cells.find(x + DIRS[d][0], y, z + DIRS[d][2], nr)) continue;
                    rises = sent(Channel::Sky, l, d, def_at(nr).opacity) > get(Channel::Sky, nr);
                }
                if (rises) queue.push_back(Node{x, y, z, 0});
            }
    spread(cells, Channel::Sky, queue);

    // Block light from every emitter.
    queue.clear();
    for (int y = 0; y < WORLD_HEIGHT; ++y)
        for (int z = 0; z < CHUNK; ++z)
            for (int x = 0; x < CHUNK; ++x) {
                cells.find(x, y, z, r);
                const int e = def_at(r).emission;
                if (e == 0) continue;
                put(Channel::Block, r, e);
                queue.push_back(Node{x, y, z, 0});
            }
    spread(cells, Channel::Block, queue);
}

void stitch_column_light(World& w, ColumnPos c) {
    if (!w.column_loaded(c)) return;
    std::vector<Change> log;
    const WorldCells cells{w, log};
    // Per border: the first cells of the two facing cell columns, and the step along the border.
    struct Border {
        ColumnPos n;
        int x0, z0, x1, z1, sx, sz;
    };
    const int bx = c.x * CHUNK, bz = c.z * CHUNK;
    const Border borders[4] = {
        {{c.x + 1, c.z}, bx + CHUNK - 1, bz, bx + CHUNK, bz, 0, 1},
        {{c.x - 1, c.z}, bx, bz, bx - 1, bz, 0, 1},
        {{c.x, c.z + 1}, bx, bz + CHUNK - 1, bx, bz + CHUNK, 1, 0},
        {{c.x, c.z - 1}, bx, bz, bx, bz - 1, 1, 0},
    };
    for (Channel ch : {Channel::Sky, Channel::Block}) {
        // Each column is already at a fixpoint on its own, and so is every border stitched before,
        // so only a border cell that would raise the facing cell can change anything. Seeding just
        // those gives the same light as seeding every lit border cell, without spreading from the
        // thousands of open-sky cells that are equal on both sides.
        std::vector<Node> queue;
        for (const Border& b : borders) {
            if (!w.column_loaded(b.n)) continue;
            const int lx0 = floor_mod(b.x0, CHUNK), lz0 = floor_mod(b.z0, CHUNK);
            const int lx1 = floor_mod(b.x1, CHUNK), lz1 = floor_mod(b.z1, CHUNK);
            for (int cy = 0; cy < SECTIONS; ++cy) {
                Chunk* c0 = w.chunk(chunk_of(b.x0, cy * CHUNK, b.z0));  // loaded columns are whole
                Chunk* c1 = w.chunk(chunk_of(b.x1, cy * CHUNK, b.z1));
                for (int ly = 0; ly < CHUNK; ++ly)
                    for (int t = 0; t < CHUNK; ++t) {
                        const int i0 = Chunk::index(lx0 + t * b.sx, ly, lz0 + t * b.sz);
                        const int i1 = Chunk::index(lx1 + t * b.sx, ly, lz1 + t * b.sz);
                        const Ref r0{c0, i0}, r1{c1, i1};
                        const int l0 = get(ch, r0), l1 = get(ch, r1);
                        const int y = cy * CHUNK + ly;
                        // Across a border is never straight down, so direction 0 (+X) stands for any.
                        if (sent(ch, l0, 0, def_at(r1).opacity) > l1)
                            queue.push_back(Node{b.x0 + t * b.sx, y, b.z0 + t * b.sz, 0});
                        if (sent(ch, l1, 0, def_at(r0).opacity) > l0)
                            queue.push_back(Node{b.x1 + t * b.sx, y, b.z1 + t * b.sz, 0});
                    }
            }
        }
        spread(cells, ch, queue);
        mark_changed_dirty(cells, ch, log);
    }
}

void relight_block(World& w, int x, int y, int z, BlockId before, BlockId after) {
    const BlockDef& a = block_def(before);
    const BlockDef& b = block_def(after);
    std::vector<Change> log;
    const WorldCells cells{w, log};
    if (a.opacity != b.opacity) relight_channel(cells, Channel::Sky, x, y, z);
    if (a.opacity != b.opacity || a.emission != b.emission) relight_channel(cells, Channel::Block, x, y, z);
}

uint8_t sky_light_at(const World& w, int x, int y, int z) {
    if (y >= WORLD_HEIGHT) return MAX_LIGHT;
    if (y < 0) return 0;
    const Chunk* c = w.chunk(chunk_of(x, y, z));
    if (c == nullptr) return 0;
    return sky_light(*c, Chunk::index(floor_mod(x, CHUNK), floor_mod(y, CHUNK), floor_mod(z, CHUNK)));
}

uint8_t block_light_at(const World& w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_HEIGHT) return 0;
    const Chunk* c = w.chunk(chunk_of(x, y, z));
    if (c == nullptr) return 0;
    return block_light(*c, Chunk::index(floor_mod(x, CHUNK), floor_mod(y, CHUNK), floor_mod(z, CHUNK)));
}
