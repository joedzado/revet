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

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "background.hpp"
#include "log.hpp"

namespace web::jobs {

inline auto now_ms() -> int64_t {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct Status {
    bool running = false;
    std::string last_result; // what the job returned, or "failed: <what()>"
    bool last_ok = false;
    int64_t started_ms = 0;
    int64_t finished_ms = 0; // 0 until a run finishes
};

// Must outlive its runs: make it static (start() runs on a background thread that uses it).
class SingleFlight {
public:
    using Job = std::function<std::string()>;

    // Runs `job` on a new thread. False (and nothing runs) if a run is in progress.
    auto start(Job job) -> bool {
        if (!begin()) {
            return false;
        }
        revet::background::spawn([this, job = std::move(job)](const std::stop_token&) -> void { finish(invoke(job)); });
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
        const std::scoped_lock lock(mu_);
        return status_;
    }

private:
    struct Outcome {
        std::string text;
        bool ok;
    };

    auto begin() -> bool {
        const std::scoped_lock lock(mu_);
        if (status_.running) {
            return false;
        }
        status_.running = true;
        status_.started_ms = now_ms();
        return true;
    }
    static auto invoke(const Job& job) -> Outcome {
        try {
            return {.text = job(), .ok = true};
        } catch (const std::exception& e) {
            return {.text = std::string("failed: ") + e.what(), .ok = false};
        } catch (...) {
            return {.text = "failed", .ok = false};
        }
    }
    void finish(Outcome o) {
        const std::scoped_lock lock(mu_);
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
    revet::background::spawn([interval, first_delay, fn = std::move(fn)](const std::stop_token& stop) -> void {
        std::mutex mu;
        std::condition_variable_any cv;
        // Sleeps for d; false once the process is shutting down.
        const auto sleep = [&](std::chrono::milliseconds d) -> bool {
            std::unique_lock lock(mu);
            (void)cv.wait_for(lock, stop, d, [] -> bool { return false; });
            return !stop.stop_requested();
        };
        for (bool running = sleep(first_delay); running; running = sleep(interval)) {
            try {
                fn();
            } catch (const std::exception& e) {
                log::error("jobs::every: {}", e.what());
            } catch (...) {
                log::error("jobs::every: unknown exception");
            }
        }
    });
}

} // namespace web::jobs
