// Async routes must get the same guards, middleware and error handling as sync
// routes. These tests run under ASan via `revet-fw test`, which is what catches
// a request that dies while its handler is suspended.
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <revet/web.hpp>
#include <stdexcept>
#include <string>

namespace {

struct IoPool {
    IoPool() {
        web::detail::io_pool() = std::make_unique<WorkerPool>(2);
        web::clear_middlewares();
    }
    ~IoPool() {
        web::detail::io_pool().reset();
        web::clear_middlewares();
    }
    IoPool(const IoPool&) = delete;
    IoPool& operator=(const IoPool&) = delete;
};

web::Request make_request(std::string auth_header = "") {
    web::Request req;
    req.method = web::Method::GET;
    if (!auth_header.empty()) {
        req.headers.data["authorization"] = std::move(auth_header);
    }
    return req;
}

// Runs dispatch_async and waits for done. Fails if done is called more than once.
web::Response run(web::Request request, const web::AsyncHandler& handler, const std::vector<web::Middleware>& guards = {}) {
    std::mutex m;
    std::condition_variable cv;
    std::optional<web::Response> result;
    std::atomic<int> calls{0};
    web::dispatch_async(std::make_shared<const web::Request>(std::move(request)), handler, guards,
                        [&](const web::Response& resp) {
                            ++calls;
                            const std::lock_guard lk(m);
                            result = resp;
                            cv.notify_one();
                        });
    std::unique_lock lk(m);
    cv.wait_for(lk, std::chrono::seconds(5), [&] { return result.has_value(); });
    REQUIRE(result.has_value());
    CHECK(calls.load() == 1);
    return *result;
}

web::Task<web::Response> ok_after_suspend(const web::Request&) {
    co_await web::run_on_io([] {});
    co_return web::Response{.body = "secret"};
}

} // namespace

TEST_CASE("async dispatch: a failing guard stops the handler", "[async]") {
    const IoPool pool;
    std::atomic<bool> ran{false};
    const web::AsyncHandler handler = [&ran](const web::Request&) -> web::Task<web::Response> {
        ran = true;
        co_return web::Response{.body = "secret"};
    };
    const auto resp = run(make_request(), handler, {web::api::bearer_static("token")});
    CHECK(resp.status == web::status::unauthorized);
    CHECK(resp.body.find("secret") == std::string::npos);
    CHECK_FALSE(ran.load());
}

TEST_CASE("async dispatch: a passing guard runs the handler", "[async]") {
    const IoPool pool;
    const auto resp = run(make_request("Bearer token"), ok_after_suspend, {web::api::bearer_static("token")});
    CHECK(resp.status == web::status::ok);
    CHECK(resp.body == "secret");
}

TEST_CASE("async dispatch: global middleware can block", "[async]") {
    const IoPool pool;
    web::use([](const web::Request&, const web::Next&) -> web::Response {
        return web::Response{.body = "blocked", .status = web::status::forbidden};
    });
    const auto resp = run(make_request(), ok_after_suspend);
    CHECK(resp.status == web::status::forbidden);
    CHECK(resp.body == "blocked");
}

TEST_CASE("async dispatch: guards run before global middleware", "[async]") {
    const IoPool pool;
    std::atomic<bool> global_ran{false};
    web::use([&global_ran](const web::Request&, const web::Next& next) -> web::Response {
        global_ran = true;
        return next();
    });
    (void)run(make_request(), ok_after_suspend, {web::api::bearer_static("token")});
    CHECK_FALSE(global_ran.load());
}

TEST_CASE("async dispatch: cookies and headers from middleware reach the response", "[async]") {
    const IoPool pool;
    web::use([](const web::Request&, const web::Next& next) -> web::Response {
        auto resp = next();
        resp.cookies.data["from_mw"] = "1";
        resp.cookies.data["both"] = "mw";
        resp.headers.data["X-From-Mw"] = "1";
        return resp;
    });
    const web::AsyncHandler handler = [](const web::Request&) -> web::Task<web::Response> {
        co_await web::run_on_io([] {});
        web::Response r{.body = "body"};
        r.cookies.data["both"] = "handler";
        co_return r;
    };
    const auto resp = run(make_request(), handler);
    CHECK(resp.body == "body");
    CHECK(resp.cookies.data.at("from_mw") == "1");
    CHECK(resp.cookies.data.at("both") == "handler");
    CHECK(resp.headers.data.at("X-From-Mw") == "1");
}

TEST_CASE("async dispatch: the request stays valid after a suspend", "[async]") {
    const IoPool pool;
    const web::AsyncHandler handler = [](const web::Request& req) -> web::Task<web::Response> {
        co_await web::run_on_io([] {});
        co_return web::Response{.body = std::string(req.headers.get("authorization", "missing"))};
    };
    const auto long_header = "Bearer " + std::string(4096, 'x');
    const auto resp = run(make_request(long_header), handler);
    CHECK(resp.body == long_header);
}

TEST_CASE("async dispatch: a throw before the first suspend gives a 500", "[async]") {
    const IoPool pool;
    const web::AsyncHandler handler = [](const web::Request&) -> web::Task<web::Response> {
        throw std::runtime_error("boom");
        co_return web::Response{};
    };
    const auto resp = run(make_request(), handler);
    CHECK(resp.status == web::status::internal_error);
    CHECK(resp.body.find("boom") == std::string::npos);
}

TEST_CASE("async dispatch: a throw after a suspend gives a 500", "[async]") {
    const IoPool pool;
    const web::AsyncHandler handler = [](const web::Request&) -> web::Task<web::Response> {
        co_await web::run_on_io([] { throw std::runtime_error("boom"); });
        co_return web::Response{};
    };
    CHECK(run(make_request(), handler).status == web::status::internal_error);
}

TEST_CASE("async dispatch: a throwing middleware gives a 500 without running the handler", "[async]") {
    const IoPool pool;
    std::atomic<bool> ran{false};
    web::use([](const web::Request&, const web::Next&) -> web::Response { throw std::runtime_error("boom"); });
    const web::AsyncHandler handler = [&ran](const web::Request&) -> web::Task<web::Response> {
        ran = true;
        co_return web::Response{};
    };
    CHECK(run(make_request(), handler).status == web::status::internal_error);
    CHECK_FALSE(ran.load());
}
