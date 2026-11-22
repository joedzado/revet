#pragma once
// Threads that outlive the code that starts them: connection handlers, scheduled jobs, the CLI's watchers.
// They are owned here instead of detached (C++ Core Guidelines CP.26). Finished threads are reaped as new
// ones start; at exit every thread is asked to stop, then joined.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace revet::background {

class Threads {
public:
    Threads() = default;
    Threads(const Threads&) = delete;
    auto operator=(const Threads&) -> Threads& = delete;
    Threads(Threads&&) = delete;
    auto operator=(Threads&&) -> Threads& = delete;

    // Asks every thread to stop, then joins them all.
    ~Threads() {
        const std::scoped_lock lock(mu_);
        std::ranges::for_each(threads_, [](Entry& e) -> void { e.thread.request_stop(); });
        threads_.clear();
    }

    // Runs f(stop_token) on a new thread. f should return soon after the token is stopped.
    template<typename F>
    void spawn(F f) {
        const auto done = std::make_shared<std::atomic<bool>>(false);
        const std::scoped_lock lock(mu_);
        std::erase_if(threads_, [](const Entry& e) -> bool { return e.done->load(); });
        // NOLINTNEXTLINE(bugprone-exception-escape): the thread body catches everything f throws
        threads_.push_back(
            {.thread = std::jthread([f = std::move(f), done](const std::stop_token& stop) mutable -> void {
                 // An escaping exception would end the process; report it and let the thread finish.
                 try {
                     f(stop);
                 } catch (const std::exception& e) {
                     (void)std::fputs("background thread failed: ", stderr);
                     (void)std::fputs(e.what(), stderr);
                     (void)std::fputc('\n', stderr);
                 } catch (...) {
                     (void)std::fputs("background thread failed\n", stderr);
                 }
                 done->store(true);
             }),
             .done = done});
    }

private:
    struct Entry {
        std::jthread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex mu_;
    std::vector<Entry> threads_;
};

// The process-wide owner. Constructed on first use, so it is destroyed (and joins) before anything
// constructed earlier, such as the logger.
inline auto threads() -> Threads& {
    static Threads owner;
    return owner;
}

// Runs f(stop_token) on a thread owned by threads().
template<typename F>
void spawn(F f) {
    threads().spawn(std::move(f));
}

} // namespace revet::background
