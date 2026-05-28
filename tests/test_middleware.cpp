#include <catch2/catch_test_macros.hpp>
#include <revet/web.hpp>
#include <string>
#include <vector>

static auto make_req(web::Method m, std::string path) -> web::Request {
    return {.method = m, .path = web::Url(std::move(path))};
}

static auto make_req_with_cookie(std::string path, std::string key, std::string val) -> web::Request {
    web::Request req;
    req.method = web::Method::GET;
    req.path = web::Url(std::move(path));
    req.cookies.data[std::move(key)] = std::move(val);
    return req;
}

// ── helpers ───────────────────────────────────────────────────────────────────

struct ScopedMiddlewares {
    ~ScopedMiddlewares() {
        web::clear_middlewares();
    }
};

// ── basic passthrough ─────────────────────────────────────────────────────────

TEST_CASE("dispatch — no middleware calls handler directly", "[middleware]") {
    ScopedMiddlewares _;
    auto handler = [](const web::Request&) { return web::to_response("ok"); };
    auto req = make_req(web::Method::GET, "/");
    auto resp = web::dispatch(req, handler);
    REQUIRE(resp.body == "ok");
}

// ── ordering ──────────────────────────────────────────────────────────────────

TEST_CASE("dispatch — middleware runs before handler", "[middleware]") {
    ScopedMiddlewares _;
    std::vector<std::string> order;

    web::use([&](const web::Request& req, web::Next next) {
        order.push_back("mw");
        return next();
    });

    auto handler = [&](const web::Request&) {
        order.push_back("handler");
        return web::to_response("ok");
    };

    web::dispatch(make_req(web::Method::GET, "/"), handler);
    REQUIRE(order == std::vector<std::string>{"mw", "handler"});
}

TEST_CASE("dispatch — multiple middleware run in registration order", "[middleware]") {
    ScopedMiddlewares _;
    std::vector<std::string> order;

    web::use([&](const web::Request&, web::Next next) {
        order.push_back("a");
        return next();
    });
    web::use([&](const web::Request&, web::Next next) {
        order.push_back("b");
        return next();
    });
    web::use([&](const web::Request&, web::Next next) {
        order.push_back("c");
        return next();
    });

    auto handler = [&](const web::Request&) {
        order.push_back("h");
        return web::to_response("ok");
    };

    web::dispatch(make_req(web::Method::GET, "/"), handler);
    REQUIRE(order == std::vector<std::string>{"a", "b", "c", "h"});
}

// ── short-circuit ─────────────────────────────────────────────────────────────

TEST_CASE("dispatch — middleware can short-circuit without calling handler", "[middleware]") {
    ScopedMiddlewares _;
    bool handler_called = false;

    web::use([](const web::Request&, web::Next) -> web::Response {
        return {.body = "blocked", .status = web::status::unauthorized};
    });

    auto handler = [&](const web::Request&) {
        handler_called = true;
        return web::to_response("ok");
    };

    auto resp = web::dispatch(make_req(web::Method::GET, "/"), handler);
    REQUIRE(resp.status == 401);
    REQUIRE(resp.body == "blocked");
    REQUIRE_FALSE(handler_called);
}

// ── auth guard pattern ────────────────────────────────────────────────────────

TEST_CASE("auth middleware — allows request with valid session cookie", "[middleware]") {
    ScopedMiddlewares _;

    web::use([](const web::Request& req, web::Next next) -> web::Response {
        if (req.cookies.get("session").empty()) {
            return {.body = "unauthorized", .status = web::status::unauthorized};
        }
        return next();
    });

    auto handler = [](const web::Request&) { return web::to_response("secret"); };
    auto req = make_req_with_cookie("/dashboard", "session", "abc123");
    auto resp = web::dispatch(req, handler);

    REQUIRE(resp.status == 200);
    REQUIRE(resp.body == "secret");
}

TEST_CASE("auth middleware — blocks request without session cookie", "[middleware]") {
    ScopedMiddlewares _;

    web::use([](const web::Request& req, web::Next next) -> web::Response {
        if (req.cookies.get("session").empty()) {
            return {.body = "unauthorized", .status = web::status::unauthorized};
        }
        return next();
    });

    auto handler = [](const web::Request&) { return web::to_response("secret"); };
    auto resp = web::dispatch(make_req(web::Method::GET, "/dashboard"), handler);

    REQUIRE(resp.status == 401);
    REQUIRE(resp.body == "unauthorized");
}

// ── response mutation ─────────────────────────────────────────────────────────

TEST_CASE("dispatch — middleware can mutate response after handler", "[middleware]") {
    ScopedMiddlewares _;

    web::use([](const web::Request&, web::Next next) {
        auto resp = next();
        resp.headers.data["X-Powered-By"] = "revet";
        return resp;
    });

    auto handler = [](const web::Request&) { return web::to_response("hello"); };
    auto resp = web::dispatch(make_req(web::Method::GET, "/"), handler);

    REQUIRE(resp.body == "hello");
    REQUIRE(resp.headers.get("X-Powered-By") == "revet");
}

// ── logging pattern ───────────────────────────────────────────────────────────

TEST_CASE("logging middleware — sees both request path and response status", "[middleware]") {
    ScopedMiddlewares _;
    std::string logged_path;
    int logged_status = 0;

    web::use([&](const web::Request& req, web::Next next) {
        auto resp = next();
        logged_path = req.path.path;
        logged_status = resp.status;
        return resp;
    });

    auto handler = [](const web::Request&) { return web::Response{.body = "data", .status = web::status::ok}; };

    web::dispatch(make_req(web::Method::GET, "/api/data"), handler);
    REQUIRE(logged_path == "/api/data");
    REQUIRE(logged_status == 200);
}

// ── stacking: wrap before + after ────────────────────────────────────────────

TEST_CASE("dispatch — middleware wraps handler symmetrically", "[middleware]") {
    ScopedMiddlewares _;
    std::vector<std::string> order;

    web::use([&](const web::Request&, web::Next next) {
        order.push_back("before-a");
        auto r = next();
        order.push_back("after-a");
        return r;
    });

    web::use([&](const web::Request&, web::Next next) {
        order.push_back("before-b");
        auto r = next();
        order.push_back("after-b");
        return r;
    });

    auto handler = [&](const web::Request&) {
        order.push_back("handler");
        return web::to_response("ok");
    };

    web::dispatch(make_req(web::Method::GET, "/"), handler);

    REQUIRE(order == std::vector<std::string>{"before-a", "before-b", "handler", "after-b", "after-a"});
}
