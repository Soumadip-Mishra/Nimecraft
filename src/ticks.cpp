#include "ticks.hpp"

#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

constexpr double TICK_SECONDS = 1.0 / BLOCK_TICKS_PER_SECOND;

uint32_t next(uint32_t& s) {
    if (s == 0) s = 0x9E3779B9u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

}  // namespace

void TickScheduler::schedule(BlockPos p, uint32_t delay_ticks) {
    const uint64_t tick = now_ + delay_ticks;
    if (!waiting_.insert(Key{tick, p.x, p.y, p.z}).second) return;
    queue_[tick].push_back(p);
}

std::vector<BlockPos> TickScheduler::due(uint64_t now, std::size_t max) {
    now_ = now;
    std::vector<BlockPos> out;
    auto it = queue_.begin();
    while (it != queue_.end() && it->first <= now && out.size() < max) {
        std::deque<BlockPos>& q = it->second;
        const std::size_t take = std::min(q.size(), max - out.size());
        for (std::size_t i = 0; i < take; ++i) {
            const BlockPos p = q[i];
            waiting_.erase(Key{it->first, p.x, p.y, p.z});
            out.push_back(p);
        }
        q.erase(q.begin(), q.begin() + static_cast<std::ptrdiff_t>(take));
        it = q.empty() ? queue_.erase(it) : std::next(it);
    }
    return out;
}

void TickScheduler::drop_column(ColumnPos c) {
    for (auto it = queue_.begin(); it != queue_.end();) {
        std::deque<BlockPos>& q = it->second;
        const uint64_t tick = it->first;
        std::erase_if(q, [&](BlockPos p) {
            if (floor_div(p.x, CHUNK) != c.x || floor_div(p.z, CHUNK) != c.z) return false;
            waiting_.erase(Key{tick, p.x, p.y, p.z});
            return true;
        });
        it = q.empty() ? queue_.erase(it) : std::next(it);
    }
}

std::size_t TickScheduler::size() const { return waiting_.size(); }

uint64_t TickScheduler::now() const { return now_; }

void schedule_around(TickScheduler& t, BlockPos p, uint32_t delay_ticks) {
    t.schedule(p, delay_ticks);
    t.schedule(BlockPos{p.x + 1, p.y, p.z}, delay_ticks);
    t.schedule(BlockPos{p.x - 1, p.y, p.z}, delay_ticks);
    t.schedule(BlockPos{p.x, p.y + 1, p.z}, delay_ticks);
    t.schedule(BlockPos{p.x, p.y - 1, p.z}, delay_ticks);
    t.schedule(BlockPos{p.x, p.y, p.z + 1}, delay_ticks);
    t.schedule(BlockPos{p.x, p.y, p.z - 1}, delay_ticks);
}

int TickClock::advance(float dt) {
    if (!std::isfinite(dt) || dt <= 0.f) return 0;
    acc += static_cast<double>(dt);
    const double whole = std::floor(acc / TICK_SECONDS);
    int steps = MAX_BLOCK_TICK_STEPS;
    if (whole < MAX_BLOCK_TICK_STEPS) {
        steps = static_cast<int>(whole);
        acc -= steps * TICK_SECONDS;
    } else {
        acc = 0;
    }
    now += static_cast<uint64_t>(steps);
    return steps;
}

void random_ticks(const World& w, uint32_t& rng, int per_chunk, std::vector<BlockPos>& out) {
    out.clear();
    if (per_chunk <= 0) return;
    std::vector<ColumnPos> cols = w.loaded_columns();
    std::sort(cols.begin(), cols.end(), [](ColumnPos a, ColumnPos b) { return a.x != b.x ? a.x < b.x : a.z < b.z; });
    for (ColumnPos c : cols)
        for (int sy = 0; sy < SECTIONS; ++sy)
            for (int i = 0; i < per_chunk; ++i) {
                const uint32_t r = next(rng);
                out.push_back(BlockPos{c.x * CHUNK + static_cast<int>(r & 15u),
                                       sy * CHUNK + static_cast<int>((r >> 4) & 15u),
                                       c.z * CHUNK + static_cast<int>((r >> 8) & 15u)});
            }
}
