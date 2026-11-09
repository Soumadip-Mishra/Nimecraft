#include "pathfind.hpp"

#include "block.hpp"
#include "light.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <tuple>

namespace {

using Key = std::tuple<int, int, int>;

constexpr int STRAIGHT = 10, DIAGONAL = 14, WATER_FACTOR = 3;

Key key(BlockPos p) {
    return {p.x, p.y, p.z};
}

BlockPos pos(const Key& k) {
    return BlockPos{std::get<0>(k), std::get<1>(k), std::get<2>(k)};
}

bool loaded(const World& w, int x, int z) {
    return w.column_loaded(column_of(chunk_of(x, 0, z)));
}

bool solid(const World& w, int x, int y, int z) {
    return block_def(w.get(x, y, z)).solid;
}

// `h` cells from y up are inside the world and not solid.
bool free_body(const World& w, int x, int y, int z, int h) {
    if (y < 0 || y + h > WORLD_HEIGHT || !loaded(w, x, z)) return false;
    for (int k = 0; k < h; ++k)
        if (solid(w, x, y + k, z)) return false;
    return true;
}

bool walkable(const World& w, int x, int y, int z, int h) {
    return free_body(w, x, y, z, h) && (solid(w, x, y - 1, z) || w.get(x, y, z) == blk::WATER);
}

int heuristic(BlockPos a, BlockPos b) {
    const int dx = std::abs(a.x - b.x), dz = std::abs(a.z - b.z);
    return STRAIGHT * std::max(dx, dz) + (DIAGONAL - STRAIGHT) * std::min(dx, dz);
}

// The cell a move from c by (dx, dz) lands on (out); false when the move is not possible.
bool target_of(const World& w, BlockPos c, int dx, int dz, const PathParams& p, int max_drop, BlockPos& out) {
    const int nx = c.x + dx, nz = c.z + dz, h = p.height;
    int ty = c.y;
    bool found = false;
    if (walkable(w, nx, c.y, nz, h)) {
        found = true;
    } else if (walkable(w, nx, c.y + 1, nz, h) && free_body(w, c.x, c.y + h, c.z, 1)) {
        ty = c.y + 1;
        found = true;
    } else if (free_body(w, nx, c.y, nz, h)) {
        for (int k = c.y - 1; k >= c.y - max_drop; --k) {
            if (k < 0 || solid(w, nx, k, nz)) break;
            if (walkable(w, nx, k, nz, h)) {
                ty = k;
                found = true;
                break;
            }
        }
    }
    if (!found) return false;
    if (dx != 0 && dz != 0) {  // no corner cutting: both side cells free over the move's height span
        const int span = std::max(c.y, ty) - c.y + h;
        if (!free_body(w, nx, c.y, c.z, span) || !free_body(w, c.x, c.y, nz, span)) return false;
    }
    out = BlockPos{nx, ty, nz};
    return true;
}

}  // namespace

int cell_light(const World& w, BlockPos p, float day_light) {
    const int sky = static_cast<int>(std::floor(static_cast<float>(sky_light_at(w, p.x, p.y, p.z)) * day_light));
    return std::max(static_cast<int>(block_light_at(w, p.x, p.y, p.z)), sky);
}

std::optional<std::vector<BlockPos>> find_path(const World& w, BlockPos start, BlockPos goal, const PathParams& p,
                                               int* expanded) {
    int count = 0;
    if (expanded != nullptr) *expanded = 0;
    if (p.height < 1 || p.max_nodes < 1) return std::nullopt;
    const int max_drop = std::max(0, p.max_drop);
    auto forbidden = [&](BlockPos c) { return cell_light(w, c, p.daylight) >= p.avoid_light_at_or_above; };
    if (!walkable(w, goal.x, goal.y, goal.z, p.height) || (goal != start && forbidden(goal))) return std::nullopt;
    if (start == goal) return std::vector<BlockPos>{start};

    // Min-heap on (f, insertion order).
    using Entry = std::tuple<int, uint64_t, Key>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    std::map<Key, int> g;
    std::map<Key, Key> parent;
    std::set<Key> closed;
    uint64_t seq = 0;
    g[key(start)] = 0;
    open.emplace(heuristic(start, goal), seq++, key(start));

    constexpr int DIRS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    while (!open.empty()) {
        const Key k = std::get<2>(open.top());
        open.pop();
        if (closed.count(k) != 0) continue;
        if (count == p.max_nodes) break;
        ++count;
        closed.insert(k);
        const BlockPos c = pos(k);
        if (c == goal) {
            std::vector<BlockPos> path{c};
            for (Key at = k; at != key(start);) {
                at = parent.at(at);
                path.push_back(pos(at));
            }
            std::reverse(path.begin(), path.end());
            if (expanded != nullptr) *expanded = count;
            return path;
        }
        const int gc = g.at(k);
        for (const auto& d : DIRS) {
            BlockPos n{};
            if (!target_of(w, c, d[0], d[1], p, max_drop, n) || forbidden(n)) continue;
            const Key nk = key(n);
            if (closed.count(nk) != 0) continue;
            int cost = (d[0] != 0 && d[1] != 0) ? DIAGONAL : STRAIGHT;
            if (w.get(n.x, n.y, n.z) == blk::WATER) cost *= WATER_FACTOR;
            const int ng = gc + cost;
            const auto it = g.find(nk);
            if (it != g.end() && it->second <= ng) continue;
            g[nk] = ng;
            parent[nk] = k;
            open.emplace(ng + heuristic(n, goal), seq++, nk);
        }
    }
    if (expanded != nullptr) *expanded = count;
    return std::nullopt;
}
