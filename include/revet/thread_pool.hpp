#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class WorkerPool {
public:
    explicit WorkerPool(int n) {
        workers_.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            workers_.emplace_back([this] { run(); });
        }
    }

    ~WorkerPool() {
        {
            std::lock_guard lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            w.join();
        }
    }

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void submit(std::function<void()> f) {
        {
            std::lock_guard lock(mu_);
            queue_.push(std::move(f));
        }
        cv_.notify_one();
    }

    [[nodiscard]] int size() const {
        return static_cast<int>(workers_.size());
    }

private:
    void run() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) {
                    return;
                }
                task = std::move(queue_.front());
                queue_.pop();
            }
            try {
                task();
            } catch (...) {
            }
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
};

// Global pool singletons — created by web::listen()
namespace web::detail {

inline auto request_pool() -> std::unique_ptr<WorkerPool>& {
    static std::unique_ptr<WorkerPool> p;
    return p;
}

inline auto io_pool() -> std::unique_ptr<WorkerPool>& {
    static std::unique_ptr<WorkerPool> p;
    return p;
}

inline void io_submit(std::function<void()> f) {
    if (auto* p = io_pool().get()) {
        p->submit(std::move(f));
    }
}

} // namespace web::detail
