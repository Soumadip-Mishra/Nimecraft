// Block ticks: the scheduled tick queue (water, falling blocks), the 20-per-second block tick clock,
// and random ticks (plant growth). Main thread only, like World.
#pragma once

#include "coords.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <tuple>
#include <vector>

class World;

constexpr int BLOCK_TICKS_PER_SECOND = 20;
constexpr std::size_t MAX_SCHEDULED_TICKS_PER_FRAME = 4096;  // the rest carry over to the next frame
constexpr int RANDOM_TICKS_PER_CHUNK = 3;
constexpr int MAX_BLOCK_TICK_STEPS = 10;  // per frame; a longer stall drops its backlog

// Positions waiting for a block tick at a given game tick. A delay counts from the clock set by
// the last due() call (0 before the first).
class TickScheduler {
public:
    // Due at now() + delay_ticks. The same position already waiting for that tick is not added again.
    void schedule(BlockPos p, uint32_t delay_ticks);
    // Sets now() to `now`, then removes and returns up to max positions due at or before it, ordered
    // by tick and, within a tick, by scheduling order. The rest stay queued ahead of later ticks.
    std::vector<BlockPos> due(uint64_t now, std::size_t max = SIZE_MAX);
    void drop_column(ColumnPos c);  // forgets every tick inside column c
    std::size_t size() const;       // positions waiting
    uint64_t now() const;

private:
    using Key = std::tuple<uint64_t, int, int, int>;  // tick, x, y, z
    std::map<uint64_t, std::deque<BlockPos>> queue_;
    std::set<Key> waiting_;
    uint64_t now_ = 0;
};

// Schedules p, then its 6 face neighbors (+X, -X, +Y, -Y, +Z, -Z), each after delay_ticks: a block
// edit at p wakes the water and falling blocks around it.
void schedule_around(TickScheduler& t, BlockPos p, uint32_t delay_ticks);

// Turns frame time into block ticks at BLOCK_TICKS_PER_SECOND.
struct TickClock {
    double acc = 0;    // seconds not yet turned into a tick, in [0, 1/20)
    uint64_t now = 0;  // block ticks run so far
    // Adds dt and returns how many ticks to run now (now advances by the same amount), at most
    // MAX_BLOCK_TICK_STEPS; time beyond the cap is dropped. dt <= 0 or non-finite adds nothing.
    int advance(float dt);
};

// Clears out, then adds per_chunk random positions inside every chunk of every loaded column
// (columns in x, then z order; chunks bottom to top). rng is xorshift32 state (0 is replaced).
void random_ticks(const World& w, uint32_t& rng, int per_chunk, std::vector<BlockPos>& out);
