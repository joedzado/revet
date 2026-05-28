#include <catch2/catch_test_macros.hpp>
#include <revet/web.hpp>
#include <string>
#include <vector>

static auto make_req(web::Method m, std::string path) -> web::Request {
    return {.method = m, .path = web::Url(std::move(path))};
}

static auto make_authed_req(std::string path) -> web::Request {
    web::Request req;
    req.method = web::Method::GET;
    req.path = web::Url(std::move(path));
    req.cookies.data["session"] = "valid";
    return req;
}

static auto run(const web::Route& route, const web::Request& req) -> web::Response {
    const web::Handler* h = nullptr;
    if (req.method == web::Method::GET && route.get) {
        h = &route.get;
    }
    if (req.method == web::Method::POST && route.post) {
        h = &route.post;
    }
    if (!h) {
        return {.status = web::status::method_not_allowed};
    }
    return web::dispatch(req, *h, route.guards);
}

// ── shared guard ──────────────────────────────────────────────────────────────

static auto require_session(const web::Request& req, web::Next next) -> web::Response {
    if (req.cookies.get("session").empty()) {
        return {.body = "unauthorized", .status = web::status::unauthorized};
    }
    return next();
}

// ── guard runs before handler ─────────────────────────────────────────────────

TEST_CASE("auth guard — blocks unauthenticated request", "[guards]") {
    web::Route route{.path = "/secret",
                     .get = [](const web::Request&) { return web::to_response("secret"); },
                     .guards = {require_session}};

    auto resp = run(route, make_req(web::Method::GET, "/secret"));
    REQUIRE(resp.status == 401);
    REQUIRE(resp.body == "unauthorized");
}

TEST_CASE("auth guard — allows authenticated request", "[guards]") {
    web::Route route{.path = "/secret",
                     .get = [](const web::Request&) { return web::to_response("secret"); },
                     .guards = {require_session}};

    auto resp = run(route, make_authed_req("/secret"));
    REQUIRE(resp.status == 200);
    REQUIRE(resp.body == "secret");
}

// ── unguarded route unaffected ────────────────────────────────────────────────

TEST_CASE("route without guards — always passes through", "[guards]") {
    web::Route route{
        .path = "/public",
        .get = [](const web::Request&) { return web::to_response("open"); },
    };

    auto resp = run(route, make_req(web::Method::GET, "/public"));
    REQUIRE(resp.status == 200);
    REQUIRE(resp.body == "open");
}

// ── guard isolation — guard on one route doesn't affect another ───────────────

TEST_CASE("auth guard — scoped to route, does not bleed to other routes", "[guards]") {
    web::Route guarded{.path = "/admin",
                       .get = [](const web::Request&) { return web::to_response("admin"); },
                       .guards = {require_session}};
    web::Route open{
        .path = "/",
        .get = [](const web::Request&) { return web::to_response("home"); },
    };

    auto blocked = run(guarded, make_req(web::Method::GET, "/admin"));
    auto allowed = run(open, make_req(web::Method::GET, "/"));

    REQUIRE(blocked.status == 401);
    REQUIRE(allowed.status == 200);
    REQUIRE(allowed.body == "home");
}

// ── multiple guards run in order ──────────────────────────────────────────────

TEST_CASE("multiple guards — run in registration order", "[guards]") {
    std::vector<std::string> order;

    web::Middleware g1 = [&](const web::Request&, web::Next next) {
        order.emplace_back("g1");
        return next();
    };
    web::Middleware g2 = [&](const web::Request&, web::Next next) {
        order.emplace_back("g2");
        return next();
    };

    web::Route route{.path = "/",
                     .get =
                         [&](const web::Request&) {
                             order.emplace_back("h");
                             return web::to_response("ok");
                         },
                     .guards = {g1, g2}};

    run(route, make_req(web::Method::GET, "/"));
    REQUIRE(order == std::vector<std::string>{"g1", "g2", "h"});
}

// ── guard short-circuits before second guard ──────────────────────────────────

TEST_CASE("guard short-circuit — stops chain, skips handler", "[guards]") {
    bool second_guard_called = false;
    bool handler_called = false;

    web::Middleware block = [](const web::Request&, web::Next) -> web::Response {
        return {.status = web::status::forbidden};
    };
    web::Middleware spy = [&](const web::Request&, web::Next next) -> web::Response {
        second_guard_called = true;
        return next();
    };

    web::Route route{.path = "/",
                     .get =
                         [&](const web::Request&) {
                             handler_called = true;
                             return web::to_response("ok");
                         },
                     .guards = {block, spy}};

    auto resp = run(route, make_req(web::Method::GET, "/"));
    REQUIRE(resp.status == 403);
    REQUIRE_FALSE(second_guard_called);
    REQUIRE_FALSE(handler_called);
}

// ── guards run before global middleware ───────────────────────────────────────

TEST_CASE("guards run before global middleware in dispatch order", "[guards]") {
    web::clear_middlewares();
    std::vector<std::string> order;

    web::use([&](const web::Request&, web::Next next) {
        order.emplace_back("global");
        return next();
    });

    web::Route route{.path = "/",
                     .get =
                         [&](const web::Request&) {
                             order.emplace_back("h");
                             return web::to_response("ok");
                         },
                     .guards = {[&](const web::Request&, web::Next next) -> web::Response {
                         order.emplace_back("guard");
                         return next();
                     }}};

    run(route, make_req(web::Method::GET, "/"));
    web::clear_middlewares();

    REQUIRE(order == std::vector<std::string>{"guard", "global", "h"});
}

// ── POST requests also guarded ────────────────────────────────────────────────

TEST_CASE("auth guard — applies to POST handlers too", "[guards]") {
    web::Route route{.path = "/submit",
                     .post = [](const web::Request&) { return web::to_response("submitted"); },
                     .guards = {require_session}};

    auto blocked = run(route, make_req(web::Method::POST, "/submit"));
    REQUIRE(blocked.status == 401);

    auto req = make_authed_req("/submit");
    req.method = web::Method::POST;
    auto allowed = run(route, req);
    REQUIRE(allowed.status == 200);
}

// ── bearer_static: constant-time fixed-token guard ─────────────────────────

TEST_CASE("bearer_static: accepts matching token", "[auth]") {
    web::Route route;
    route.path   = "/secret";
    route.get    = [](const web::Request&) { return web::to_response("OK"); };
    route.guards = { web::api::bearer_static("supersecret") };

    web::Request req = make_req(web::Method::GET, "/secret");
    req.headers.data["authorization"] = "Bearer supersecret";
    REQUIRE(run(route, req).status == 200);
}

TEST_CASE("bearer_static: rejects mismatched token", "[auth]") {
    web::Route route;
    route.path   = "/secret";
    route.get    = [](const web::Request&) { return web::to_response("OK"); };
    route.guards = { web::api::bearer_static("supersecret") };

    web::Request req = make_req(web::Method::GET, "/secret");
    req.headers.data["authorization"] = "Bearer wrong";
    REQUIRE(run(route, req).status == 401);
}

TEST_CASE("bearer_static: rejects missing Authorization header", "[auth]") {
    web::Route route;
    route.path   = "/secret";
    route.get    = [](const web::Request&) { return web::to_response("OK"); };
    route.guards = { web::api::bearer_static("supersecret") };

    web::Request req = make_req(web::Method::GET, "/secret");
    REQUIRE(run(route, req).status == 401);
}

TEST_CASE("bearer_static: empty expected token fails closed", "[auth][security]") {
    // Common pitfall: API_KEY env var is unset → expected = "". Must not
    // accept any caller (including those who send Bearer "" or no token).
    web::Route route;
    route.path   = "/secret";
    route.get    = [](const web::Request&) { return web::to_response("OK"); };
    route.guards = { web::api::bearer_static("") };

    SECTION("no header") {
        web::Request req = make_req(web::Method::GET, "/secret");
        REQUIRE(run(route, req).status == 401);
    }
    SECTION("empty bearer") {
        web::Request req = make_req(web::Method::GET, "/secret");
        req.headers.data["authorization"] = "Bearer ";
        REQUIRE(run(route, req).status == 401);
    }
    SECTION("any token") {
        web::Request req = make_req(web::Method::GET, "/secret");
        req.headers.data["authorization"] = "Bearer hunter2";
        REQUIRE(run(route, req).status == 401);
    }
}

TEST_CASE("bearer_static: rejects token with wrong length", "[auth][security]") {
    web::Route route;
    route.path   = "/secret";
    route.get    = [](const web::Request&) { return web::to_response("OK"); };
    route.guards = { web::api::bearer_static("supersecret") };

    web::Request req = make_req(web::Method::GET, "/secret");
    req.headers.data["authorization"] = "Bearer supersecret-extra";
    REQUIRE(run(route, req).status == 401);
}
