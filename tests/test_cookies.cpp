#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <revet/web.hpp>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

static std::string capture_response(const web::Response& resp) {
    std::array<int, 2> fds{};
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data());
    web::send_response(fds[0], resp);
    close(fds[0]);
    std::string buf(4096, '\0');
    auto n = recv(fds[1], buf.data(), buf.size() - 1, 0);
    close(fds[1]);
    if (n > 0) {
        buf.resize(static_cast<size_t>(n));
    } else {
        buf.clear();
    }
    return buf;
}

TEST_CASE("Cookie Parsing", "[web]") {
    // Mock a socket pair to feed data into parse_request
    std::array<int, 2> fds{};
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data());

    const char* raw_request = "GET / HTTP/1.1\r\n"
                              "Host: localhost\r\n"
                              "Cookie: user=joe;theme=dark;_cw_SentAlert=GgAAAE1lc3NhZ2Ugc2VudCE=\r\n"
                              "\r\n";

    send(fds[1] /* NOLINT */, raw_request, strlen(raw_request), 0);
    const auto req = web::parse_request(fds[0] /* NOLINT */);

    REQUIRE(req.cookies.get("user") == "joe");
    REQUIRE(req.cookies.get("theme") == "dark");
    REQUIRE(req.cookies.get("_cw_SentAlert") == "GgAAAE1lc3NhZ2Ugc2VudCE=");

    close(fds[0] /* NOLINT */);
    close(fds[1] /* NOLINT */);
}

TEST_CASE("send_response strips CRLF from Location header", "[web]") {
    auto resp = web::redirect("/legit\r\nX-Injected: evil");
    std::string raw = capture_response(resp);
    // CRLF stripped: no injected header line should appear
    REQUIRE(raw.find("\r\nX-Injected:") == std::string::npos);
    // The Location header itself is present (value has CRLF removed)
    REQUIRE(raw.find("Location:") != std::string::npos);
}

TEST_CASE("Set-Cookie includes HttpOnly and SameSite but not Secure", "[web]") {
    web::Response resp = web::to_response("ok");
    resp.cookies.data["session"] = "abc123";
    std::string raw = capture_response(resp);
    REQUIRE(raw.find("Set-Cookie:") != std::string::npos);
    REQUIRE(raw.find("HttpOnly") != std::string::npos);
    REQUIRE(raw.find("SameSite=Lax") != std::string::npos);
    REQUIRE(raw.find("Secure") == std::string::npos);
}

TEST_CASE("Clearing a cookie uses Max-Age=0", "[web]") {
    web::Response resp = web::to_response("ok");
    resp.cookies.data["_cw_Flash"] = "";
    std::string raw = capture_response(resp);
    REQUIRE(raw.find("_cw_Flash=;") != std::string::npos);
    REQUIRE(raw.find("Max-Age=0") != std::string::npos);
}

TEST_CASE("web::back() does not follow external Referer", "[web]") {
    web::Request req;
    req.headers.data["referer"] = "https://evil.example.com/steal";
    auto resp = web::back(req);
    REQUIRE(resp.location == "/");
}

TEST_CASE("web::back() follows same-origin Referer", "[web]") {
    web::Request req;
    req.headers.data["referer"] = "/dashboard";
    auto resp = web::back(req);
    REQUIRE(resp.location == "/dashboard");
}

TEST_CASE("web::back() defaults to / when Referer absent", "[web]") {
    web::Request req;
    auto resp = web::back(req);
    REQUIRE(resp.location == "/");
}

TEST_CASE("send_response delivers complete large body", "[web]") {
    std::string large_body(128UL * 1024UL, 'x'); // 128 KB
    web::Response resp = web::to_response(large_body);

    std::array<int, 2> fds{};
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data());

    // Send in a thread to avoid deadlock when body exceeds socket buffer
    std::jthread sender([&]() {
        web::send_response(fds[0], resp);
        close(fds[0]);
    });

    std::string received;
    received.resize(512UL * 1024UL);
    ssize_t total = 0;
    ssize_t n = 0;
    while ((n = recv(fds[1], received.data() + static_cast<size_t>(total), received.size() - static_cast<size_t>(total),
                     0)) > 0) {
        total += n;
    }
    close(fds[1]);
    received.resize(static_cast<size_t>(total));

    auto sep = received.find("\r\n\r\n");
    REQUIRE(sep != std::string::npos);
    std::string body = received.substr(sep + 4);
    REQUIRE(body.size() == large_body.size());
    REQUIRE(body == large_body);
}
