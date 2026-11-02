// A mutex-guarded FIFO queue and a fixed-size thread pool.
// Jobs never touch World (Global Constraints): they get copies and hand results back through a
// ConcurrentQueue that the main thread drains.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

template <class T>
class ConcurrentQueue {
public:
    void push(T v) {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.push_back(std::move(v));
    }

    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (items_.empty()) return std::nullopt;
        std::optional<T> v(std::move(items_.front()));
        items_.pop_front();
        return v;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

private:
    mutable std::mutex mutex_;
    std::deque<T> items_;
};

class ThreadPool {
public:
    explicit ThreadPool(unsigned threads);  // >= 1; 0 is treated as 1
    ~ThreadPool();                          // finishes queued jobs, joins
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(std::function<void()> job);
    void wait_idle();  // blocks until queue empty and no job running
    unsigned size() const;

private:
    void worker();

    std::mutex mutex_;
    std::condition_variable work_cv_;  // a job was queued, or the pool is stopping
    std::condition_variable idle_cv_;  // a job finished
    std::deque<std::function<void()>> jobs_;
    unsigned running_ = 0;
    bool stopping_ = false;
    std::vector<std::thread> threads_;
};
