#pragma once
// Background work an app runs besides serving requests.
//
//   SingleFlight: one run at a time, with the last outcome for the layout to show.
//     static web::jobs::SingleFlight sync;
//     if (!sync.start([] { return run_sync(); })) return "already running";
//     auto s = sync.status();   // s.running, s.last_result, s.last_ok, s.finished_ms
//
//   every: run `fn` on a background thread at a fixed interval, for the life of the process.
//     web::jobs::every(std::chrono::hours{24}, [] { backup(); });

#include "log.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace web::jobs {

inline auto now_ms() -> int64_t {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

struct Status {
    bool running = false;
    std::string last_result; // what the job returned, or "failed: <what()>"
    bool last_ok = false;
    int64_t started_ms = 0;
    int64_t finished_ms = 0; // 0 until a run finishes
};

// Must outlive its runs: make it static (start() runs on a detached thread that uses it).
class SingleFlight {
public:
    using Job = std::function<std::string()>;

    // Runs `job` on a new thread. False (and nothing runs) if a run is in progress.
    auto start(Job job) -> bool {
        if (!begin()) {
            return false;
        }
        std::thread([this, job = std::move(job)] { finish(invoke(job)); }).detach();
        return true;
    }

    // Runs `job` on this thread. False if a run is in progress.
    auto run(const Job& job) -> bool {
        if (!begin()) {
            return false;
        }
        finish(invoke(job));
        return true;
    }

    auto status() -> Status {
        const std::lock_guard lock(mu_);
        return status_;
    }

private:
    struct Outcome {
        std::string text;
        bool ok;
    };

    auto begin() -> bool {
        const std::lock_guard lock(mu_);
        if (status_.running) {
            return false;
        }
        status_.running = true;
        status_.started_ms = now_ms();
        return true;
    }
    static auto invoke(const Job& job) -> Outcome {
        try {
            return {job(), true};
        } catch (const std::exception& e) {
            return {std::string("failed: ") + e.what(), false};
        } catch (...) {
            return {"failed", false};
        }
    }
    void finish(Outcome o) {
        const std::lock_guard lock(mu_);
        status_.running = false;
        status_.last_result = std::move(o.text);
        status_.last_ok = o.ok;
        status_.finished_ms = now_ms();
    }

    std::mutex mu_;
    Status status_;
};

// Calls `fn` every `interval` on a background thread, first after `first_delay`.
// An exception is logged and the schedule continues. Runs until the process exits.
inline void every(std::chrono::milliseconds interval, std::function<void()> fn,
                  std::chrono::milliseconds first_delay = std::chrono::milliseconds{0}) {
    std::thread([interval, first_delay, fn = std::move(fn)] {
        std::this_thread::sleep_for(first_delay);
        for (;;) {
            try {
                fn();
            } catch (const std::exception& e) {
                ::log::error("jobs::every: {}", e.what());
            } catch (...) {
                ::log::error("jobs::every: unknown exception");
            }
            std::this_thread::sleep_for(interval);
        }
    }).detach();
}

} // namespace web::jobs
