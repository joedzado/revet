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

static std::string exception_message(std::exception_ptr p) {
    try {
        std::rethrow_exception(p);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown";
    }
}

// ── Task<T> exception propagation ────────────────────────────────────────────

TEST_CASE("Task<int> exception propagates via co_await", "[task]") {
    auto thrower = []() -> web::Task<int> {
        throw std::runtime_error("task boom");
        co_return 0;
    };

    std::atomic<bool> done{false};
    std::exception_ptr caught;

    auto driver = [&]() -> web::Task<int> {
        try {
            co_return co_await thrower();
        } catch (...) {
            caught = std::current_exception();
            co_return -1;
        }
    };

    std::move(driver()).start([&](int) { done.store(true); });

    REQUIRE(done.load());
    REQUIRE(caught != nullptr);
    REQUIRE(exception_message(caught) == "task boom");
}

TEST_CASE("Task<void> exception propagates via co_await", "[task]") {
    auto thrower = []() -> web::Task<void> {
        throw std::runtime_error("void boom");
        co_return;
    };

    std::atomic<bool> done{false};
    std::exception_ptr caught;

    auto driver = [&]() -> web::Task<void> {
        try {
            co_await thrower();
        } catch (...) {
            caught = std::current_exception();
        }
        co_return;
    };

    std::move(driver()).start([&]() { done.store(true); });

    REQUIRE(done.load());
    REQUIRE(caught != nullptr);
    REQUIRE(exception_message(caught) == "void boom");
}

TEST_CASE("Task<int> returns value normally", "[task]") {
    auto producer = []() -> web::Task<int> { co_return 42; };

    std::atomic<bool> done{false};
    int result = 0;

    auto driver = [&]() -> web::Task<int> { co_return co_await producer(); };

    std::move(driver()).start([&](int v) {
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
    IOPoolFixture() {
        web::detail::io_pool() = std::make_unique<WorkerPool>(2);
    }
    ~IOPoolFixture() {
        web::detail::io_pool().reset();
    }
};

TEST_CASE("IOAwaitable exception propagates via co_await", "[task]") {
    IOPoolFixture pool;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> done{false};
    std::exception_ptr caught;

    auto driver = [&]() -> web::Task<int> {
        try {
            co_await web::run_on_io([]() -> int {
                throw std::runtime_error("io boom");
                return 0;
            });
        } catch (...) {
            caught = std::current_exception();
        }
        co_return 0;
    };

    std::move(driver()).start([&](int) {
        std::unique_lock lk(m);
        done.store(true);
        cv.notify_one();
    });

    std::unique_lock lk(m);
    cv.wait_for(lk, std::chrono::seconds(5), [&] { return done.load(); });

    REQUIRE(done.load());
    REQUIRE(caught != nullptr);
    REQUIRE(exception_message(caught) == "io boom");
}

TEST_CASE("IOAwaitable returns value normally", "[task]") {
    IOPoolFixture pool;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> done{false};
    int result = 0;

    auto driver = [&]() -> web::Task<int> { co_return co_await web::run_on_io([]() -> int { return 99; }); };

    std::move(driver()).start([&](int v) {
        std::unique_lock lk(m);
        result = v;
        done.store(true);
        cv.notify_one();
    });

    std::unique_lock lk(m);
    cv.wait_for(lk, std::chrono::seconds(5), [&] { return done.load(); });

    REQUIRE(done.load());
    REQUIRE(result == 99);
}
