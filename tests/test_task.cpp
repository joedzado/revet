#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <revet/task.hpp>
#include <revet/thread_pool.hpp>
#include <stdexcept>
#include <string>
#include <utility>

// Handlers take the request by reference (web::AsyncHandler); run_async_handler keeps it alive across
// suspensions, and each test waits for its coroutines before captured locals go out of scope.
// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters,cppcoreguidelines-avoid-capturing-lambda-coroutines)

namespace {
auto exception_message(std::exception_ptr p) -> std::string {
    try {
        std::rethrow_exception(std::move(p));
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown";
    }
}
} // namespace

// ── Task<T> exception propagation ────────────────────────────────────────────

TEST_CASE("Task<int> exception propagates via co_await", "[task]") {
    auto thrower = [] -> web::Task<int> {
        throw std::runtime_error("task boom");
        co_return 0;
    };

    std::atomic<bool> done{false};
    std::exception_ptr caught;

    const auto driver = [&] -> web::Task<int> {
        try {
            co_return co_await thrower();
        } catch (...) {
            caught = std::current_exception();
            co_return -1;
        }
    };

    std::move(driver()).start([&](int) -> void { done.store(true); });

    REQUIRE(done.load());
    REQUIRE(caught != nullptr);
    REQUIRE(exception_message(caught) == "task boom");
}

TEST_CASE("Task<void> exception propagates via co_await", "[task]") {
    auto thrower = [] -> web::Task<void> {
        throw std::runtime_error("void boom");
        co_return;
    };

    std::atomic<bool> done{false};
    std::exception_ptr caught;

    const auto driver = [&] -> web::Task<void> {
        try {
            co_await thrower();
        } catch (...) {
            caught = std::current_exception();
        }
        co_return;
    };

    std::move(driver()).start([&] -> void { done.store(true); });

    REQUIRE(done.load());
    REQUIRE(caught != nullptr);
    REQUIRE(exception_message(caught) == "void boom");
}

TEST_CASE("Task<int> returns value normally", "[task]") {
    auto producer = [] -> web::Task<int> { co_return 42; };

    std::atomic<bool> done{false};
    int result = 0;

    const auto driver = [&] -> web::Task<int> { co_return co_await producer(); };

    std::move(driver()).start([&](int v) -> void {
        result = v;
        done.store(true);
    });

    REQUIRE(done.load());
    REQUIRE(result == 42);
}

// ── IOAwaitable exception propagation ────────────────────────────────────────
// IOAwaitable submits to io_pool, which is normally initialized by web::listen().
// Initialize it directly for these tests.

struct IOPoolFixture {
    IOPoolFixture(const IOPoolFixture&) = delete;
    auto operator=(const IOPoolFixture&) -> IOPoolFixture& = delete;
    IOPoolFixture(IOPoolFixture&&) = delete;
    auto operator=(IOPoolFixture&&) -> IOPoolFixture& = delete;
    IOPoolFixture() {
        web::detail::io_pool() = std::make_unique<WorkerPool>(2);
    }
    ~IOPoolFixture() {
        web::detail::io_pool().reset();
    }
};

TEST_CASE("IOAwaitable exception propagates via co_await", "[task]") {
    const IOPoolFixture pool;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> done{false};
    std::exception_ptr caught;

    const auto driver = [&] -> web::Task<int> {
        try {
            co_await web::run_on_io([] -> int {
                throw std::runtime_error("io boom");
                return 0;
            });
        } catch (...) {
            caught = std::current_exception();
        }
        co_return 0;
    };

    std::move(driver()).start([&](int) -> void {
        const std::unique_lock lk(m);
        done.store(true);
        cv.notify_one();
    });

    std::unique_lock lk(m);
    cv.wait_for(lk, std::chrono::seconds(5), [&] -> bool { return done.load(); });

    REQUIRE(done.load());
    REQUIRE(caught != nullptr);
    REQUIRE(exception_message(caught) == "io boom");
}

TEST_CASE("IOAwaitable returns value normally", "[task]") {
    const IOPoolFixture pool;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> done{false};
    int result = 0;

    const auto driver = [&] -> web::Task<int> { co_return co_await web::run_on_io([] -> int { return 99; }); };

    std::move(driver()).start([&](int v) -> void {
        const std::unique_lock lk(m);
        result = v;
        done.store(true);
        cv.notify_one();
    });

    std::unique_lock lk(m);
    cv.wait_for(lk, std::chrono::seconds(5), [&] -> bool { return done.load(); });

    REQUIRE(done.load());
    REQUIRE(result == 99);
}
// NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters,cppcoreguidelines-avoid-capturing-lambda-coroutines)
