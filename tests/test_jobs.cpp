// Tests for src/jobs.*: ConcurrentQueue and ThreadPool.
#include "jobs.hpp"
#include "test.hpp"

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <thread>
#include <vector>

TEST(test_queue_fifo) {
    ConcurrentQueue<int> q;
    CHECK(q.size() == 0);
    q.push(1);
    q.push(2);
    q.push(3);
    CHECK(q.size() == 3);
    CHECK(q.try_pop() == 1);
    CHECK(q.try_pop() == 2);
    CHECK(q.try_pop() == 3);
    CHECK(!q.try_pop().has_value());
    CHECK(q.size() == 0);
}

TEST(test_queue_move_only) {
    ConcurrentQueue<std::unique_ptr<int>> q;
    q.push(std::make_unique<int>(7));
    auto v = q.try_pop();
    CHECK(v.has_value() && *v && **v == 7);
}

TEST(test_queue_concurrent) {
    ConcurrentQueue<int> q;
    constexpr int PER = 10000, PRODUCERS = 4;
    std::vector<int> seen(static_cast<std::size_t>(PER * PRODUCERS), 0);
    std::vector<std::thread> producers;
    for (int t = 0; t < PRODUCERS; ++t)
        producers.emplace_back([&q, t] {
            for (int i = 0; i < PER; ++i) q.push(t * PER + i);
        });
    std::thread consumer([&q, &seen] {
        int got = 0;
        while (got < PER * PRODUCERS) {
            if (auto v = q.try_pop()) {
                ++seen[static_cast<std::size_t>(*v)];
                ++got;
            } else {
                std::this_thread::yield();
            }
        }
    });
    for (auto& p : producers) p.join();
    consumer.join();
    bool all_once = true;
    for (int s : seen) all_once = all_once && s == 1;
    CHECK(all_once);
    CHECK(q.size() == 0);
}

TEST(test_jobs_runs_all) {
    std::atomic<int> counter{0};
    ThreadPool pool(4);
    CHECK(pool.size() == 4);
    for (int i = 0; i < 1000; ++i) pool.submit([&counter] { counter.fetch_add(1); });
    pool.wait_idle();
    CHECK(counter.load() == 1000);
}

TEST(test_jobs_zero_jobs) {
    ThreadPool pool(3);
    pool.wait_idle();  // returns at once: nothing queued, nothing running
    CHECK(pool.size() == 3);
    ThreadPool one(1);
    CHECK(one.size() == 1);
}

TEST(test_jobs_zero_threads_clamped) {
    // The precondition is >= 1 thread; 0 is clamped to 1 so submitted jobs still run.
    ThreadPool pool(0);
    CHECK(pool.size() == 1);
    std::atomic<int> counter{0};
    pool.submit([&counter] { counter.fetch_add(1); });
    pool.wait_idle();
    CHECK(counter.load() == 1);
}

TEST(test_jobs_destructor_finishes_queued) {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(2);
        for (int i = 0; i < 100; ++i) pool.submit([&counter] { counter.fetch_add(1); });
    }
    CHECK(counter.load() == 100);
}

TEST(test_jobs_wait_idle_waits_for_running_job) {
    ThreadPool pool(1);
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    std::atomic<bool> done{false};
    pool.submit([gate, &done] {
        gate.wait();
        done.store(true);
    });
    std::thread releaser([&release] { release.set_value(); });
    pool.wait_idle();
    CHECK(done.load());
    releaser.join();
}

TEST(test_jobs_submit_from_job) {
    ThreadPool pool(2);
    std::atomic<int> counter{0};
    pool.submit([&pool, &counter] {
        counter.fetch_add(1);
        pool.submit([&counter] { counter.fetch_add(1); });
    });
    pool.wait_idle();
    CHECK(counter.load() == 2);
}
