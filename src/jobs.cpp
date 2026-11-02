#include "jobs.hpp"

#include <algorithm>

ThreadPool::ThreadPool(unsigned threads) {
    const unsigned n = std::max(1u, threads);
    threads_.reserve(n);
    for (unsigned i = 0; i < n; ++i) threads_.emplace_back([this] { worker(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    work_cv_.notify_all();
    for (std::thread& t : threads_) t.join();
}

void ThreadPool::submit(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    work_cv_.notify_one();
}

void ThreadPool::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_cv_.wait(lock, [this] { return jobs_.empty() && running_ == 0; });
}

unsigned ThreadPool::size() const {
    return static_cast<unsigned>(threads_.size());
}

void ThreadPool::worker() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        work_cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (jobs_.empty()) return;  // stopping, and every queued job has run
        std::function<void()> job = std::move(jobs_.front());
        jobs_.pop_front();
        ++running_;
        lock.unlock();
        job();
        lock.lock();
        --running_;
        if (jobs_.empty() && running_ == 0) idle_cv_.notify_all();
    }
}
