#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <revet/web.hpp>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// Send raw HTTP text into a socketpair and parse it
static auto parse(const char* raw) -> web::Request {
    std::array<int, 2> fds{};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) != 0) {
        return {};
    }
    send(fds[1] /* NOLINT */, raw, strlen(raw), 0);
    close(fds[1] /* NOLINT */);
    const auto req = web::parse_request(fds[0] /* NOLINT */);
    close(fds[0] /* NOLINT */);
    return req;
}

// ── method parsing ────────────────────────────────────────────────────────────

TEST_CASE("parse_request — GET", "[request]") {
    const auto r = parse("GET / HTTP/1.1\r\n\r\n");
    CHECK(r.method == web::Method::GET);
    CHECK(std::string_view(r.path) == "/");
}

TEST_CASE("parse_request — POST", "[request]") {
    const auto r = parse("POST /submit HTTP/1.1\r\n\r\n");
    CHECK(r.method == web::Method::POST);
    CHECK(std::string_view(r.path) == "/submit");
}

TEST_CASE("parse_request — PUT", "[request]") {
    const auto r = parse("PUT /items/7 HTTP/1.1\r\n\r\n");
    CHECK(r.method == web::Method::PUT);
    CHECK(std::string_view(r.path) == "/items/7");
}

TEST_CASE("parse_request — DELETE", "[request]") {
    const auto r = parse("DELETE /items/7 HTTP/1.1\r\n\r\n");
    CHECK(r.method == web::Method::DEL);
    CHECK(std::string_view(r.path) == "/items/7");
}

TEST_CASE("parse_request — PATCH", "[request]") {
    const auto r = parse("PATCH /items/7 HTTP/1.1\r\n\r\n");
    CHECK(r.method == web::Method::PATCH);
    CHECK(std::string_view(r.path) == "/items/7");
}

// ── query string parsing ──────────────────────────────────────────────────────

TEST_CASE("parse_request — single query param", "[request]") {
    const auto r = parse("GET /?q=hello HTTP/1.1\r\n\r\n");
    CHECK(r.query.get("q") == "hello");
}

TEST_CASE("parse_request — multiple query params", "[request]") {
    const auto r = parse("GET /?a=1&b=2 HTTP/1.1\r\n\r\n");
    CHECK(r.query.get("a") == "1");
    CHECK(r.query.get("b") == "2");
}

// ── form parsing ──────────────────────────────────────────────────────────────

TEST_CASE("parse_request — parses url-encoded form data", "[request]") {
    const auto r = parse("POST / HTTP/1.1\r\n"
                         "Content-Type: application/x-www-form-urlencoded\r\n"
                         "Content-Length: 15\r\n\r\n"
                         "foo=bar&baz=qux");
    CHECK(r.form.get("foo") == "bar");
    CHECK(r.form.get("baz") == "qux");
}

TEST_CASE("parse_request — form data is url-decoded", "[request]") {
    const auto r = parse("POST / HTTP/1.1\r\n"
                         "Content-Type: application/x-www-form-urlencoded\r\n"
                         "Content-Length: 12\r\n\r\n"
                         "msg=hi+there");
    CHECK(r.form.get("msg") == "hi there");
}

// ── redirect safety ───────────────────────────────────────────────────────────

TEST_CASE("redirect_safe — allows internal path", "[request]") {
    const auto r = web::redirect_safe("/dashboard");
    CHECK(r.location == "/dashboard");
}

TEST_CASE("redirect_safe — blocks absolute URL", "[request]") {
    const auto r = web::redirect_safe("http://evil.com");
    CHECK(r.location == "/");
}

// ── body size limit ───────────────────────────────────────────────────────────

TEST_CASE("parse_request — body within limit accepted", "[request]") {
    const auto r = parse("POST / HTTP/1.1\r\n"
                         "Content-Length: 5\r\n\r\n"
                         "hello");
    CHECK(!r.body_too_large);
}

TEST_CASE("parse_request — Content-Length over limit sets body_too_large", "[request]") {
    // Claim 3 MB body (over 2 MB limit) — don't actually send that much data
    const auto r = parse("POST / HTTP/1.1\r\n"
                         "Content-Length: 3145728\r\n\r\n");
    CHECK(r.body_too_large);
    CHECK(r.form.data.empty());
}

// ── known bugs (hidden from default run, tag [known-bug]) ────────────────────

TEST_CASE("parse_request handles Cookie header > 8192 bytes", "[request]") {
    std::string big_value(9000, 'a');
    std::string raw = "GET / HTTP/1.1\r\nHost: localhost\r\nCookie: session=" + big_value + "\r\n\r\n";

    std::array<int, 2> fds{};
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) == 0);

    // Send in a thread — avoids deadlock when request exceeds socket send buffer
    std::jthread sender([&raw, &fds]() {
        const char* p = raw.data();
        size_t remaining = raw.size();
        while (remaining > 0) {
            ssize_t sent = ::send(fds[1], p, remaining, 0);
            if (sent <= 0) {
                break;
            }
            p += sent;
            remaining -= static_cast<size_t>(sent);
        }
        close(fds[1]);
    });

    const auto r = web::parse_request(fds[0]);
    close(fds[0]);
    CHECK(r.cookies.get("session") == big_value);
}
