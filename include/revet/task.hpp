#pragma once

#include <coroutine>
#include <cstdio>
#include <exception>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

#include "thread_pool.hpp"

namespace web {

// ── Task<T> ───────────────────────────────────────────────────────────────────
// Coroutine return type for async route handlers.
// initial_suspend = always → caller controls when execution starts via start().
// Supports both fire-and-forget (start + callback) and co_await chaining.

// Runs a completion callback inside final_suspend, which must not throw: a failure is reported, not propagated.
template<typename F>
void run_callback(F&& f) noexcept {
    try {
        std::forward<F>(f)();
    } catch (const std::exception& e) {
        (void)std::fputs("task completion callback failed: ", stderr);
        (void)std::fputs(e.what(), stderr);
        (void)std::fputc('\n', stderr);
    } catch (...) {
        (void)std::fputs("task completion callback failed\n", stderr);
    }
}

template<typename T>
class Task {
public:
    auto operator=(Task&&) -> Task& = delete;
    struct promise_type {
        std::optional<T> result;
        std::exception_ptr exception;
        std::function<void(T)> on_done;       // fire-and-forget completion
        std::coroutine_handle<> continuation; // parent when co_awaited

        auto get_return_object() -> Task {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        auto initial_suspend() noexcept -> std::suspend_always {
            return {};
        }

        struct Final {
            auto await_ready() noexcept -> bool {
                return false;
            }
            void await_resume() noexcept {}
            auto await_suspend(std::coroutine_handle<promise_type> h) noexcept -> std::coroutine_handle<> {
                auto& p = h.promise();
                if (p.continuation) {
                    return p.continuation; // resume parent
                }
                // top-level: fire callback, self-destruct
                if (p.on_done && p.result) {
                    run_callback([&] -> void { p.on_done(std::move(p.result).value()); });
                }
                h.destroy();
                return std::noop_coroutine();
            }
        };
        auto final_suspend() noexcept -> Final {
            return {};
        }

        void return_value(T v) {
            result = std::move(v);
        }
        void unhandled_exception() {
            exception = std::current_exception();
        }
    };

    using Handle = std::coroutine_handle<promise_type>;

    explicit Task(Handle h) : h_(h) {}
    Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    ~Task() {
        if (h_) {
            h_.destroy();
        }
    }
    Task(const Task&) = delete;
    auto operator=(const Task&) -> Task& = delete;

    // Fire-and-forget: attach callback, start coroutine, release ownership.
    // The coroutine self-destructs in Final::await_suspend when done.
    void start(std::function<void(T)> cb) && {
        h_.promise().on_done = std::move(cb);
        std::exchange(h_, {}).resume();
    }

    // Awaitable interface for co_await Task inside another coroutine
    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return false;
    }
    auto await_suspend(std::coroutine_handle<> cont) noexcept -> std::coroutine_handle<> {
        h_.promise().continuation = cont;
        return h_; // symmetric transfer — don't release h_ (destructor cleans up)
    }
    auto await_resume() -> T {
        if (h_.promise().exception) {
            std::rethrow_exception(h_.promise().exception);
        }
        return std::move(h_.promise().result).value();
    }

private:
    Handle h_;
};

// ── Task<void> ────────────────────────────────────────────────────────────────
template<>
class Task<void> {
public:
    auto operator=(Task&&) -> Task& = delete;
    struct promise_type {
        std::exception_ptr exception;
        std::function<void()> on_done;
        std::coroutine_handle<> continuation;

        auto get_return_object() -> Task {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        auto initial_suspend() noexcept -> std::suspend_always {
            return {};
        }

        struct Final {
            auto await_ready() noexcept -> bool {
                return false;
            }
            void await_resume() noexcept {}
            auto await_suspend(std::coroutine_handle<promise_type> h) noexcept -> std::coroutine_handle<> {
                const auto& p = h.promise();
                if (p.continuation) {
                    return p.continuation;
                }
                if (p.on_done) {
                    run_callback([&] -> void { p.on_done(); });
                }
                h.destroy();
                return std::noop_coroutine();
            }
        };
        auto final_suspend() noexcept -> Final {
            return {};
        }

        void return_void() {}
        void unhandled_exception() {
            exception = std::current_exception();
        }
    };

    using Handle = std::coroutine_handle<promise_type>;

    explicit Task(Handle h) : h_(h) {}
    Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    ~Task() {
        if (h_) {
            h_.destroy();
        }
    }
    Task(const Task&) = delete;
    auto operator=(const Task&) -> Task& = delete;

    void start(std::function<void()> cb) && {
        h_.promise().on_done = std::move(cb);
        std::exchange(h_, {}).resume();
    }

    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return false;
    }
    auto await_suspend(std::coroutine_handle<> cont) noexcept -> std::coroutine_handle<> {
        h_.promise().continuation = cont;
        return h_;
    }
    void await_resume() {
        if (h_.promise().exception) {
            std::rethrow_exception(h_.promise().exception);
        }
    }

private:
    Handle h_;
};

// ── IOAwaitable ───────────────────────────────────────────────────────────────
// Submits blocking work to the I/O pool, suspends caller, resumes when done.

template<typename F>
class IOAwaitable {
    using R = std::invoke_result_t<F>;
    using Storage = std::conditional_t<std::is_void_v<R>, std::monostate, std::optional<R>>;

public:
    explicit IOAwaitable(F fn) : fn_(std::move(fn)) {}

    auto await_ready() -> bool {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) {
        detail::io_submit([this, h] mutable -> auto {
            try {
                if constexpr (std::is_void_v<R>) {
                    fn_();
                } else {
                    result_.emplace(fn_());
                }
            } catch (...) {
                exception_ = std::current_exception();
            }
            h.resume();
        });
    }

    auto await_resume() -> R {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
        if constexpr (!std::is_void_v<R>) {
            return std::move(result_).value();
        }
    }

private:
    F fn_;
    Storage result_;
    std::exception_ptr exception_;
};

// run_on_io(fn) — co_await this to execute fn() on the I/O pool
template<typename F>
auto run_on_io(F&& fn) -> IOAwaitable<std::decay_t<F>> {
    return IOAwaitable<std::decay_t<F>>(std::forward<F>(fn));
}

} // namespace web
