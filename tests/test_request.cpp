#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <revet/web.hpp>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// Send raw HTTP text into a socketpair and parse it
namespace {
auto parse(const char* raw) -> web::Request {
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
} // namespace

// ── method parsing ────────────────────────────────────────────────────────────

TEST_CASE("parse_request — GET", "[request]") {
    const auto r = parse("GET / HTTP/1.1\r\n\r\n");
    CHECK(r.method == web::Method::GET);
    CHECK(r.http_version == "HTTP/1.1");
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
    // Claim 100 MiB (over the 64 MiB default) — don't actually send that much data
    const auto r = parse("POST / HTTP/1.1\r\n"
                         "Content-Length: 104857600\r\n\r\n");
    CHECK(r.body_too_large);
    CHECK(r.form.data.empty());
}

TEST_CASE("parse_request — default limit is 64 MiB, not 2 MB", "[request]") {
    CHECK(web::config::max_body_bytes() == web::config::DEFAULT_MAX_BODY_BYTES);
    CHECK(web::config::DEFAULT_MAX_BODY_BYTES == 64UL * 1024 * 1024);
    // A 3 MB claim used to be rejected; it now passes the size check.
    const auto r = parse("POST / HTTP/1.1\r\n"
                         "Content-Length: 3145728\r\n\r\n");
    CHECK(!r.body_too_large);
}

TEST_CASE("parse_request — configured limit applies", "[request]") {
    web::config::max_body_bytes(1024);
    const auto big = parse("POST / HTTP/1.1\r\n"
                           "Content-Length: 2048\r\n\r\n");
    const auto small = parse("POST / HTTP/1.1\r\n"
                             "Content-Length: 5\r\n\r\n"
                             "hello");
    web::config::max_body_bytes(web::config::DEFAULT_MAX_BODY_BYTES);
    CHECK(big.body_too_large);
    CHECK(!small.body_too_large);
}

TEST_CASE("parse_size — sizes with binary units", "[request]") {
    using web::detail::parse_size;
    CHECK(parse_size("1048576") == 1048576UL);
    CHECK(parse_size("64MB") == 64UL * 1024 * 1024);
    CHECK(parse_size("512k") == 512UL * 1024);
    CHECK(parse_size("1.5GiB") == static_cast<size_t>(1.5 * 1024 * 1024 * 1024));
    CHECK(parse_size(" 2 mb ") == 2UL * 1024 * 1024);
    CHECK(!parse_size(""));
    CHECK(!parse_size("lots"));
    CHECK(!parse_size("10 parsecs"));
    CHECK(!parse_size("0"));
}

// ── known bugs (hidden from default run, tag [known-bug]) ────────────────────

TEST_CASE("parse_request — body is cut to Content-Length", "[request]") {
    const auto r = parse("POST / HTTP/1.1\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: 3\r\n\r\n"
                         "abcdef");
    CHECK(r.raw_body == "abc");
}

TEST_CASE("parse_request — explicit zero length drops buffered bytes", "[request]") {
    const auto r = parse("POST / HTTP/1.1\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: 0\r\n\r\n"
                         "hello");
    CHECK(std::string_view(r.path) == "/");
    CHECK(r.raw_body.empty());
    CHECK(!r.body_too_large);
}

TEST_CASE("parse_request — PUT json body", "[request]") {
    const auto r = parse("PUT /items/7 HTTP/1.1\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: 7\r\n\r\n"
                         "{\"a\":1}");
    CHECK(r.method == web::Method::PUT);
    CHECK(r.raw_body == "{\"a\":1}");
}

TEST_CASE("parse_request — oversized body is not parsed", "[request]") {
    web::config::max_body_bytes(8);
    const auto r = parse("POST / HTTP/1.1\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: 100\r\n\r\n"
                         "{\"a\":1}");
    web::config::max_body_bytes(web::config::DEFAULT_MAX_BODY_BYTES);
    CHECK(r.body_too_large);
    CHECK(r.raw_body.empty());
    CHECK(r.form.data.empty());
    CHECK(std::string_view(r.path) == "/");
}

TEST_CASE("parse_request — rejects smuggled lengths and folded headers", "[request]") {
    const auto dropped = [](const char* raw) -> void {
        const auto r = parse(raw);
        CHECK(r.path.path.empty());
        CHECK(r.raw_body.empty());
        CHECK(r.form.data.empty());
    };
    dropped("POST / HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello");
    dropped("POST / HTTP/1.1\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\nhello");
    dropped("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
    dropped("POST / HTTP/1.1\r\nContent-Length: 5abc\r\n\r\nhello");
    dropped("POST / HTTP/1.1\r\nContent-Length: +5\r\n\r\nhello");
    dropped("POST / HTTP/1.1\r\nContent-Length: 5 0\r\n\r\nhello");
    dropped("GET / HTTP/1.1\r\nHost: example.com\r\nHost: evil.com\r\n\r\n");
    dropped("POST / HTTP/1.1\r\nContent-Length: 1\r\n 5\r\n\r\nhello");
    dropped("GET / HTTP/1.1\r\nHost: example.com");
}

TEST_CASE("parse_request handles Cookie header > 8192 bytes", "[request]") {
    std::string big_value(9000, 'a');
    std::string raw = "GET / HTTP/1.1\r\nHost: localhost\r\nCookie: session=" + big_value + "\r\n\r\n";

    std::array<int, 2> fds{};
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) == 0);

    // Send in a thread — avoids deadlock when request exceeds socket send buffer
    const std::jthread sender([&raw, &fds] -> void {
        std::string_view remaining = raw;
        while (!remaining.empty()) {
            const ssize_t sent = ::send(fds.at(1), remaining.data(), remaining.size(), 0);
            if (sent <= 0) {
                break;
            }
            remaining.remove_prefix(static_cast<size_t>(sent));
        }
        close(fds.at(1));
    });

    const auto r = web::parse_request(fds.at(0));
    close(fds.at(0));
    CHECK(r.cookies.get("session") == big_value);
}
