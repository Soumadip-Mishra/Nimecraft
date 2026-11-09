// Tests for src/ticks.*: the scheduled block tick queue, the block tick clock, and random ticks.
#include "coords.hpp"
#include "test.hpp"
#include "ticks.hpp"
#include "world.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

bool in_loaded_chunk(const World& w, BlockPos p) {
    return p.y >= 0 && p.y < WORLD_HEIGHT && w.chunk(chunk_of(p.x, p.y, p.z)) != nullptr;
}

}  // namespace

// ---- TickScheduler ----

TEST(test_tick_order) {
    TickScheduler t;
    const BlockPos a{1, 64, 1}, b{2, 64, 2};
    t.schedule(a, 5);
    t.schedule(b, 2);
    const std::vector<BlockPos> first = t.due(2);
    CHECK(first.size() == 1);
    CHECK(first.size() == 1 && first[0] == b);
    const std::vector<BlockPos> second = t.due(5);
    CHECK(second.size() == 1 && second[0] == a);

    // Two at the same tick come out in scheduling order, whatever their coordinates.
    const BlockPos c{9, 70, 9}, d{-3, 10, -3};
    t.schedule(c, 3);
    t.schedule(d, 3);
    const std::vector<BlockPos> same = t.due(8);
    CHECK(same.size() == 2);
    CHECK(same.size() == 2 && same[0] == c && same[1] == d);
}

TEST(test_tick_order_earlier_tick_before_later_scheduled_first) {
    // Everything due comes out tick by tick, even when several ticks are due in one call.
    TickScheduler t;
    t.schedule(BlockPos{1, 1, 1}, 7);
    t.schedule(BlockPos{2, 2, 2}, 3);
    t.schedule(BlockPos{3, 3, 3}, 5);
    const std::vector<BlockPos> v = t.due(10);
    CHECK(v.size() == 3);
    CHECK(v.size() == 3 && v[0] == (BlockPos{2, 2, 2}) && v[1] == (BlockPos{3, 3, 3}) && v[2] == (BlockPos{1, 1, 1}));
    CHECK(t.size() == 0);
}

TEST(test_tick_not_early) {
    TickScheduler t;
    CHECK(t.due(100).empty());  // sets the scheduler's clock to 100
    CHECK(t.now() == 100);
    t.schedule(BlockPos{0, 64, 0}, 5);
    CHECK(t.due(104).empty());
    CHECK(t.size() == 1);
    const std::vector<BlockPos> v = t.due(105);
    CHECK(v.size() == 1 && v[0] == (BlockPos{0, 64, 0}));
    CHECK(t.size() == 0);
}

TEST(test_tick_zero_delay_due_now) {
    TickScheduler t;
    t.due(40);
    t.schedule(BlockPos{0, 1, 0}, 0);
    CHECK(t.due(40).size() == 1);
}

TEST(test_tick_dedupe) {
    TickScheduler t;
    t.schedule(BlockPos{4, 64, 4}, 5);
    t.schedule(BlockPos{4, 64, 4}, 5);
    CHECK(t.size() == 1);
    const std::vector<BlockPos> v = t.due(5);
    CHECK(v.size() == 1);
    CHECK(t.due(100).empty());

    // The same position for two different ticks is kept twice, once per tick.
    t.schedule(BlockPos{4, 64, 4}, 2);
    t.schedule(BlockPos{4, 64, 4}, 3);
    CHECK(t.size() == 2);
    CHECK(t.due(102).size() == 1);
    CHECK(t.due(103).size() == 1);

    // Once returned, the same position may be scheduled again for the same tick.
    t.schedule(BlockPos{4, 64, 4}, 0);
    CHECK(t.due(103).size() == 1);
}

TEST(test_tick_budget_carryover) {
    TickScheduler t;
    for (int i = 0; i < 5000; ++i) t.schedule(BlockPos{i, 64, 0}, 1);
    const std::vector<BlockPos> first = t.due(1, 4096);
    CHECK(first.size() == 4096);
    CHECK(first.size() == 4096 && first[0] == (BlockPos{0, 64, 0}) && first[4095] == (BlockPos{4095, 64, 0}));
    CHECK(t.size() == 904);
    const std::vector<BlockPos> second = t.due(2, 4096);
    CHECK(second.size() == 904);
    CHECK(second.size() == 904 && second[0] == (BlockPos{4096, 64, 0}) && second[903] == (BlockPos{4999, 64, 0}));
    CHECK(t.size() == 0);
    CHECK(t.due(3, 4096).empty());
}

TEST(test_tick_budget_boundaries) {
    TickScheduler t;
    for (int i = 0; i < 3; ++i) t.schedule(BlockPos{i, 1, 0}, 0);
    CHECK(t.due(0, 0).empty());  // max 0 takes nothing
    CHECK(t.size() == 3);
    CHECK(t.due(0, 3).size() == 3);  // exactly the count
    CHECK(t.size() == 0);
    for (int i = 0; i < 3; ++i) t.schedule(BlockPos{i, 1, 0}, 0);
    CHECK(t.due(0, 2).size() == 2);  // one below the count
    CHECK(t.size() == 1);
    CHECK(t.due(0, 4).size() == 1);  // one above the count
}

TEST(test_tick_carryover_runs_before_newer_ticks) {
    // A position left over by the budget stays ahead of ticks that fall due later.
    TickScheduler t;
    t.schedule(BlockPos{1, 1, 1}, 1);
    t.schedule(BlockPos{2, 2, 2}, 1);
    CHECK(t.due(1, 1).size() == 1);
    t.schedule(BlockPos{3, 3, 3}, 1);  // due at tick 2
    const std::vector<BlockPos> v = t.due(2);
    CHECK(v.size() == 2 && v[0] == (BlockPos{2, 2, 2}) && v[1] == (BlockPos{3, 3, 3}));
}

TEST(test_tick_empty_scheduler) {
    TickScheduler t;
    CHECK(t.size() == 0);
    CHECK(t.now() == 0);
    CHECK(t.due(0).empty());
    CHECK(t.due(1000000).empty());
    t.drop_column(ColumnPos{0, 0});  // nothing to drop
    CHECK(t.size() == 0);
}

TEST(test_tick_drop_column) {
    TickScheduler t;
    t.schedule(BlockPos{15, 64, 0}, 1);  // column 0
    t.schedule(BlockPos{16, 64, 0}, 1);  // column 1
    t.schedule(BlockPos{-1, 64, 0}, 1);  // column -1
    t.drop_column(ColumnPos{0, 0});
    CHECK(t.size() == 2);
    const std::vector<BlockPos> v = t.due(1);
    CHECK(v.size() == 2 && v[0] == (BlockPos{16, 64, 0}) && v[1] == (BlockPos{-1, 64, 0}));
}

TEST(test_tick_drop_column_negative_z_and_reschedule) {
    TickScheduler t;
    t.schedule(BlockPos{0, 10, -1}, 4);   // column (0, -1)
    t.schedule(BlockPos{0, 10, -16}, 4);  // column (0, -1)
    t.schedule(BlockPos{0, 10, -17}, 4);  // column (0, -2)
    t.schedule(BlockPos{0, 10, 0}, 4);    // column (0, 0)
    t.drop_column(ColumnPos{0, -1});
    CHECK(t.size() == 2);
    // A dropped position can be scheduled again for the same tick.
    t.schedule(BlockPos{0, 10, -1}, 4);
    CHECK(t.size() == 3);
    CHECK(t.due(4).size() == 3);
}

TEST(test_tick_far_coordinates_and_late_clock) {
    TickScheduler t;
    const uint64_t late = 5000000000ull;  // past 2^32 ticks
    t.due(late);
    t.schedule(BlockPos{1000000, 255, -1000000}, 20);
    t.drop_column(ColumnPos{62499, -62500});  // the neighbor, not its column
    CHECK(t.due(late + 19).empty());
    const std::vector<BlockPos> v = t.due(late + 20);
    CHECK(v.size() == 1 && v[0] == (BlockPos{1000000, 255, -1000000}));
    t.schedule(BlockPos{1000000, 0, -1000000}, 1);
    t.drop_column(ColumnPos{62500, -62500});
    CHECK(t.size() == 0);
}

// ---- TickClock ----

TEST(test_tick_clock_twenty_per_second) {
    TickClock c;
    CHECK(c.advance(1.f) == 10);  // one second, but at most 10 steps per frame
    CHECK(c.now == 10);
    TickClock d;
    int steps = 0;
    for (int i = 0; i < 60; ++i) steps += d.advance(1.f / 60.f);
    CHECK(steps == 20);  // 60 frames of 1/60 s
    CHECK(d.now == 20);
}

TEST(test_tick_clock_boundaries) {
    TickClock c;
    CHECK(c.advance(0.049f) == 0);  // just under one tick (0.05 s)
    CHECK(c.advance(0.002f) == 1);  // crosses it
    CHECK(c.now == 1);
    TickClock d;
    CHECK(d.advance(0.5f) == 10);  // exactly the cap
    TickClock e;
    CHECK(e.advance(0.55f) == 10);  // above the cap: the backlog is dropped, not carried
    CHECK(e.advance(0.01f) == 0);
    CHECK(e.now == 10);
}

TEST(test_tick_clock_bad_dt) {
    TickClock c;
    CHECK(c.advance(0.f) == 0);
    CHECK(c.advance(-1.f) == 0);
    CHECK(c.advance(std::numeric_limits<float>::quiet_NaN()) == 0);
    CHECK(c.advance(std::numeric_limits<float>::infinity()) == 0);
    CHECK(c.now == 0);
    CHECK(c.advance(0.05f) == 1);  // still works afterwards
}

// ---- random_ticks ----

TEST(test_random_ticks_count) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, Column{});
    w.insert_column(ColumnPos{-1, 3}, Column{});
    uint32_t rng = 12345;
    std::vector<BlockPos> out;
    random_ticks(w, rng, 3, out);
    CHECK(out.size() == 96);
    bool all_loaded = true;
    for (BlockPos p : out) all_loaded = all_loaded && in_loaded_chunk(w, p);
    CHECK(all_loaded);
    CHECK(rng != 12345u);

    // 3 per chunk: every one of the 32 chunks gets exactly 3.
    bool three_each = true;
    for (int cx : {0, -1})
        for (int sy = 0; sy < SECTIONS; ++sy) {
            const int cz = cx == 0 ? 0 : 3;
            int n = 0;
            for (BlockPos p : out)
                if (chunk_of(p.x, p.y, p.z) == (ChunkPos{cx, sy, cz})) ++n;
            three_each = three_each && n == 3;
        }
    CHECK(three_each);

    uint32_t rng2 = 12345;
    std::vector<BlockPos> again{BlockPos{7, 7, 7}};  // cleared first
    random_ticks(w, rng2, 3, again);
    CHECK(again == out);
    CHECK(rng2 == rng);
}

TEST(test_random_ticks_order_independent) {
    // Columns loaded in a different order give the same positions for the same rng.
    World a(1), b(1);
    a.insert_column(ColumnPos{2, -5}, Column{});
    a.insert_column(ColumnPos{-7, 1}, Column{});
    a.insert_column(ColumnPos{0, 0}, Column{});
    b.insert_column(ColumnPos{0, 0}, Column{});
    b.insert_column(ColumnPos{-7, 1}, Column{});
    b.insert_column(ColumnPos{2, -5}, Column{});
    uint32_t ra = 99, rb = 99;
    std::vector<BlockPos> va, vb;
    random_ticks(a, ra, 3, va);
    random_ticks(b, rb, 3, vb);
    CHECK(va.size() == 144);
    CHECK(va == vb);
}

TEST(test_random_ticks_empty_and_zero) {
    World w(1);
    uint32_t rng = 5;
    std::vector<BlockPos> out{BlockPos{1, 2, 3}};
    random_ticks(w, rng, 3, out);  // nothing loaded
    CHECK(out.empty());
    w.insert_column(ColumnPos{0, 0}, Column{});
    random_ticks(w, rng, 0, out);
    CHECK(out.empty());
    random_ticks(w, rng, -2, out);
    CHECK(out.empty());
}

TEST(test_random_ticks_zero_rng_and_far_column) {
    World w(1);
    w.insert_column(ColumnPos{62500, -62500}, Column{});  // x = 1,000,000 .. 1,000,015
    uint32_t rng = 0;  // replaced, never stuck at 0
    std::vector<BlockPos> out;
    random_ticks(w, rng, 3, out);
    CHECK(out.size() == 48);
    CHECK(rng != 0u);
    bool inside = true, varied = false;
    for (BlockPos p : out) {
        inside = inside && p.x >= 1000000 && p.x <= 1000015 && p.z >= -1000000 && p.z <= -999985 && p.y >= 0 && p.y <= 255;
        varied = varied || !(p == out[0]);
    }
    CHECK(inside);
    CHECK(varied);
}

// ---- schedule_around ----

TEST(test_schedule_around) {
    TickScheduler t;
    t.due(10);
    schedule_around(t, BlockPos{0, 0, -1}, 5);
    CHECK(t.size() == 7);
    CHECK(t.due(14).empty());
    const std::vector<BlockPos> v = t.due(15);
    CHECK(v.size() == 7);
    const std::vector<BlockPos> want = {{0, 0, -1}, {1, 0, -1}, {-1, 0, -1}, {0, 1, -1},
                                        {0, -1, -1}, {0, 0, 0}, {0, 0, -2}};
    CHECK(v == want);
}
