#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <revet/web.hpp>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

// Drives a single request through the full parse → dispatch → send pipeline
// using a socketpair, matching what the real server loop does.
static std::string http_roundtrip(const std::vector<web::Route>& routes, const std::string& raw_request,
                                  const std::vector<web::Middleware>& middlewares = {}) {
    std::array<int, 2> fds{};
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data());

    std::jthread server([&]() {
        auto req = web::parse_request(fds[0]);
        web::Response resp;
        for (const auto& r : routes) {
            if (r.path != std::string_view(req.path)) {
                continue;
            }
            web::Handler h;
            if (req.method == web::Method::GET && r.get) {
                h = r.get;
            } else if (req.method == web::Method::POST && r.post) {
                h = r.post;
            }
            if (h) {
                resp = web::dispatch(req, h, middlewares);
                goto done;
            }
        }
        resp = {.body = "404 Not Found", .status = web::status::not_found};
    done:
        web::send_response(fds[0], resp);
        close(fds[0]);
    });

    send(fds[1], raw_request.data(), raw_request.size(), 0);
    shutdown(fds[1], SHUT_WR);

    std::string buf(32 * 1024, '\0');
    ssize_t total = 0, n = 0;
    while ((n = recv(fds[1], buf.data() + total, buf.size() - static_cast<size_t>(total), 0)) > 0) {
        total += n;
    }
    close(fds[1]);
    buf.resize(static_cast<size_t>(total));
    return buf;
}

// ── basic routing ─────────────────────────────────────────────────────────────

TEST_CASE("integration: GET returns 200 with body", "[integration]") {
    std::vector<web::Route> routes = {
        {.path = "/hello", .get = [](const web::Request&) { return web::to_response("world"); }}};
    auto resp = http_roundtrip(routes, "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(resp.find("world") != std::string::npos);
}

TEST_CASE("integration: unknown route returns 404", "[integration]") {
    std::vector<web::Route> routes = {
        {.path = "/hello", .get = [](const web::Request&) { return web::to_response("world"); }}};
    auto resp = http_roundtrip(routes, "GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(resp.find("HTTP/1.1 404") != std::string::npos);
}

TEST_CASE("integration: POST reads form data", "[integration]") {
    std::string body_str = "name=alice&age=30";
    std::string req = "POST /submit HTTP/1.1\r\n"
                      "Host: localhost\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\n"
                      "Content-Length: " +
                      std::to_string(body_str.size()) + "\r\n\r\n" + body_str;

    std::string captured_name;
    std::vector<web::Route> routes = {{.path = "/submit", .post = [&](const web::Request& r) {
                                           captured_name = r.form.get("name");
                                           return web::to_response("ok");
                                       }}};
    auto resp = http_roundtrip(routes, req);
    REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(captured_name == "alice");
}

// ── middleware ────────────────────────────────────────────────────────────────

TEST_CASE("integration: middleware runs before handler", "[integration]") {
    std::vector<std::string> order;
    web::Middleware mw = [&](const web::Request& req, web::Next next) {
        order.push_back("mw");
        return next();
    };
    std::vector<web::Route> routes = {{.path = "/", .get = [&](const web::Request&) {
                                           order.push_back("h");
                                           return web::to_response("ok");
                                       }}};
    http_roundtrip(routes, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", {mw});
    REQUIRE(order == std::vector<std::string>{"mw", "h"});
}

TEST_CASE("integration: middleware can short-circuit handler", "[integration]") {
    web::Middleware blocker = [](const web::Request&, web::Next) {
        web::Response r = web::to_response("blocked");
        r.status = web::status::forbidden;
        return r;
    };
    std::vector<web::Route> routes = {
        {.path = "/secret", .get = [](const web::Request&) { return web::to_response("secret"); }}};
    auto resp = http_roundtrip(routes, "GET /secret HTTP/1.1\r\nHost: localhost\r\n\r\n", {blocker});
    REQUIRE(resp.find("HTTP/1.1 403") != std::string::npos);
    REQUIRE(resp.find("blocked") != std::string::npos);
    REQUIRE(resp.find("secret") == std::string::npos);
}

// ── response headers ──────────────────────────────────────────────────────────

TEST_CASE("integration: redirect response sets Location header", "[integration]") {
    std::vector<web::Route> routes = {
        {.path = "/old", .get = [](const web::Request&) { return web::redirect("/new"); }}};
    auto resp = http_roundtrip(routes, "GET /old HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(resp.find("Location: /new") != std::string::npos);
    REQUIRE(resp.find("HTTP/1.1 303") != std::string::npos);
}

TEST_CASE("integration: cookie set in response survives wire", "[integration]") {
    std::vector<web::Route> routes = {{.path = "/login", .get = [](const web::Request&) {
                                           web::Response r = web::to_response("ok");
                                           r.cookies.data["token"] = "abc123";
                                           return r;
                                       }}};
    auto resp = http_roundtrip(routes, "GET /login HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(resp.find("Set-Cookie: token=abc123") != std::string::npos);
    REQUIRE(resp.find("HttpOnly") != std::string::npos);
    REQUIRE(resp.find("Secure") == std::string::npos);
}

TEST_CASE("integration: query params parsed correctly", "[integration]") {
    std::string captured;
    std::vector<web::Route> routes = {{.path = "/search", .get = [&](const web::Request& r) {
                                           captured = r.query.get("q");
                                           return web::to_response("ok");
                                       }}};
    http_roundtrip(routes, "GET /search?q=hello+world HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(captured == "hello world");
}
