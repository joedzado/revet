#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <print>
#include <queue>
#include <thread>
#include <vector>

class WorkerPool {
public:
    WorkerPool(WorkerPool&&) = delete;
    auto operator=(WorkerPool&&) -> WorkerPool& = delete;
    explicit WorkerPool(int n) {
        workers_.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            workers_.emplace_back([this] -> void { run(); });
        }
    }

    ~WorkerPool() {
        {
            const std::scoped_lock lock(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            w.join();
        }
    }

    WorkerPool(const WorkerPool&) = delete;
    auto operator=(const WorkerPool&) -> WorkerPool& = delete;

    void submit(std::function<void()> f) {
        {
            const std::scoped_lock lock(mu_);
            queue_.push(std::move(f));
        }
        cv_.notify_one();
    }

    [[nodiscard]] auto size() const -> int {
        return static_cast<int>(workers_.size());
    }

private:
    void run() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mu_);
                cv_.wait(lock, [this] -> bool { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) {
                    return;
                }
                task = std::move(queue_.front());
                queue_.pop();
            }
            try {
                task();
            } catch (const std::exception& e) {
                std::println(stderr, "worker task failed: {}", e.what());
            } catch (...) {
                std::println(stderr, "worker task failed: unknown exception");
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
