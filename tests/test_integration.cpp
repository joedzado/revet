#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <revet/web.hpp>
#include <span>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

// Drives a single request through the full parse → dispatch → send pipeline
// using a socketpair, matching what the real server loop does.
static auto http_roundtrip(const std::vector<web::Route>& routes, const std::string& raw_request,
                           const std::vector<web::Middleware>& middlewares = {}) -> std::string {
    std::array<int, 2> fds{};
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data());

    const std::jthread server([&]() -> void {
        const auto req = web::parse_request(fds.at(0));
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
        web::send_response(fds.at(0), resp);
        close(fds.at(0));
    });

    send(fds.at(1), raw_request.data(), raw_request.size(), 0);
    shutdown(fds.at(1), SHUT_WR);

    std::string buf(32 * 1024, '\0');
    ssize_t total = 0, n = 0;
    while ((n = recv(fds.at(1), std::span(buf).subspan(static_cast<size_t>(total)).data(),
                     buf.size() - static_cast<size_t>(total), 0)) > 0) {
        total += n;
    }
    close(fds.at(1));
    buf.resize(static_cast<size_t>(total));
    return buf;
}

// ── basic routing ─────────────────────────────────────────────────────────────

TEST_CASE("integration: GET returns 200 with body", "[integration]") {
    const std::vector<web::Route> routes = {
        {.path = "/hello", .get = [](const web::Request&) -> web::Response { return web::to_response("world"); }}};
    const auto resp = http_roundtrip(routes, "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(resp.contains("HTTP/1.1 200"));
    REQUIRE(resp.contains("world"));
}

TEST_CASE("integration: unknown route returns 404", "[integration]") {
    const std::vector<web::Route> routes = {
        {.path = "/hello", .get = [](const web::Request&) -> web::Response { return web::to_response("world"); }}};
    const auto resp = http_roundtrip(routes, "GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(resp.contains("HTTP/1.1 404"));
}

TEST_CASE("integration: POST reads form data", "[integration]") {
    const std::string body_str = "name=alice&age=30";
    const std::string req = "POST /submit HTTP/1.1\r\n"
                            "Host: localhost\r\n"
                            "Content-Type: application/x-www-form-urlencoded\r\n"
                            "Content-Length: " +
                            std::to_string(body_str.size()) + "\r\n\r\n" + body_str;

    std::string captured_name;
    const std::vector<web::Route> routes = {{.path = "/submit", .post = [&](const web::Request& r) -> web::Response {
                                                 captured_name = r.form.get("name");
                                                 return web::to_response("ok");
                                             }}};
    const auto resp = http_roundtrip(routes, req);
    REQUIRE(resp.contains("HTTP/1.1 200"));
    REQUIRE(captured_name == "alice");
}

// ── middleware ────────────────────────────────────────────────────────────────

TEST_CASE("integration: middleware runs before handler", "[integration]") {
    std::vector<std::string> order;
    const web::Middleware mw = [&](const web::Request& req, web::Next next) -> web::Response {
        order.emplace_back("mw");
        return next();
    };
    const std::vector<web::Route> routes = {{.path = "/", .get = [&](const web::Request&) -> web::Response {
                                                 order.emplace_back("h");
                                                 return web::to_response("ok");
                                             }}};
    http_roundtrip(routes, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", {mw});
    REQUIRE(order == std::vector<std::string>{"mw", "h"});
}

TEST_CASE("integration: middleware can short-circuit handler", "[integration]") {
    const web::Middleware blocker = [](const web::Request&, web::Next) -> web::Response {
        web::Response r = web::to_response("blocked");
        r.status = web::status::forbidden;
        return r;
    };
    const std::vector<web::Route> routes = {
        {.path = "/secret", .get = [](const web::Request&) -> web::Response { return web::to_response("secret"); }}};
    const auto resp = http_roundtrip(routes, "GET /secret HTTP/1.1\r\nHost: localhost\r\n\r\n", {blocker});
    REQUIRE(resp.contains("HTTP/1.1 403"));
    REQUIRE(resp.contains("blocked"));
    REQUIRE(!resp.contains("secret"));
}

// ── response headers ──────────────────────────────────────────────────────────

TEST_CASE("integration: redirect response sets Location header", "[integration]") {
    const std::vector<web::Route> routes = {
        {.path = "/old", .get = [](const web::Request&) -> web::Response { return web::redirect("/new"); }}};
    const auto resp = http_roundtrip(routes, "GET /old HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(resp.contains("Location: /new"));
    REQUIRE(resp.contains("HTTP/1.1 303"));
}

TEST_CASE("integration: cookie set in response survives wire", "[integration]") {
    const std::vector<web::Route> routes = {{.path = "/login", .get = [](const web::Request&) -> web::Response {
                                                 web::Response r = web::to_response("ok");
                                                 r.cookies.data["token"] = "abc123";
                                                 return r;
                                             }}};
    const auto resp = http_roundtrip(routes, "GET /login HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(resp.contains("Set-Cookie: token=abc123"));
    REQUIRE(resp.contains("HttpOnly"));
    REQUIRE(!resp.contains("Secure"));
}

TEST_CASE("integration: query params parsed correctly", "[integration]") {
    std::string captured;
    const std::vector<web::Route> routes = {{.path = "/search", .get = [&](const web::Request& r) -> web::Response {
                                                 captured = r.query.get("q");
                                                 return web::to_response("ok");
                                             }}};
    http_roundtrip(routes, "GET /search?q=hello+world HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(captured == "hello world");
}
