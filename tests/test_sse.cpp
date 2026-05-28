#include <catch2/catch_test_macros.hpp>
#include <revet/web.hpp>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static auto make_socketpair() -> std::pair<int, int> {
    int fds[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    return {fds[0], fds[1]};
}

static auto read_all(int fd, int timeout_ms = 200) -> std::string {
    std::string out;
    char buf[4096];
    struct timeval tv{0, timeout_ms * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            break;
        }
        out.append(buf, static_cast<size_t>(n));
    }
    return out;
}

TEST_CASE("SSEWriter::send emits correct event format") {
    auto [writer_fd, reader_fd] = make_socketpair();
    web::SSEWriter sse{writer_fd};

    sse.send("hello world");
    ::shutdown(writer_fd, SHUT_WR);

    auto got = read_all(reader_fd);
    REQUIRE(got == "data: hello world\n\n");

    ::close(writer_fd);
    ::close(reader_fd);
}

TEST_CASE("SSEWriter::send includes event and id fields") {
    auto [writer_fd, reader_fd] = make_socketpair();
    web::SSEWriter sse{writer_fd};

    sse.send("payload", "update", "42");
    ::shutdown(writer_fd, SHUT_WR);

    auto got = read_all(reader_fd);
    REQUIRE(got.find("id: 42\n") != std::string::npos);
    REQUIRE(got.find("event: update\n") != std::string::npos);
    REQUIRE(got.find("data: payload\n") != std::string::npos);

    ::close(writer_fd);
    ::close(reader_fd);
}

TEST_CASE("SSEWriter::send splits multi-line data") {
    auto [writer_fd, reader_fd] = make_socketpair();
    web::SSEWriter sse{writer_fd};

    sse.send("line1\nline2");
    ::shutdown(writer_fd, SHUT_WR);

    auto got = read_all(reader_fd);
    REQUIRE(got.find("data: line1\n") != std::string::npos);
    REQUIRE(got.find("data: line2\n") != std::string::npos);

    ::close(writer_fd);
    ::close(reader_fd);
}

TEST_CASE("SSEWriter::comment emits SSE comment") {
    auto [writer_fd, reader_fd] = make_socketpair();
    web::SSEWriter sse{writer_fd};

    sse.comment("keepalive");
    ::shutdown(writer_fd, SHUT_WR);

    auto got = read_all(reader_fd);
    REQUIRE(got == ": keepalive\n\n");

    ::close(writer_fd);
    ::close(reader_fd);
}

TEST_CASE("SSEWriter::alive returns true on open socket") {
    auto [writer_fd, reader_fd] = make_socketpair();
    web::SSEWriter sse{writer_fd};
    REQUIRE(sse.alive());
    ::close(writer_fd);
    ::close(reader_fd);
}

TEST_CASE("SSEWriter::alive returns false after peer closes") {
    auto [writer_fd, reader_fd] = make_socketpair();
    web::SSEWriter sse{writer_fd};
    ::close(reader_fd);
    // Give OS time to propagate FIN
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE_FALSE(sse.alive());
    ::close(writer_fd);
}

TEST_CASE("Route with sse handler sends SSE headers and streams") {
    auto [server_fd, client_fd] = make_socketpair();

    std::vector<web::Route> routes;
    routes.push_back({
        .path = "/events",
        .sse = [](const web::Request&, web::SSEWriter& sse) { sse.send("tick", "update"); },
    });

    // Simulate what listen() does for an SSE route
    web::Request req;
    req.method = web::Method::GET;
    req.path = web::Url("/events");

    auto it = std::ranges::find_if(
        routes, [&req](const web::Route& r) { return web::match_route(r.path, req.path.path, req.params); });

    REQUIRE(it != routes.end());
    REQUIRE(it->sse);

    const std::string_view sse_head = "HTTP/1.1 200 OK\r\n"
                                      "Content-Type: text/event-stream\r\n"
                                      "Cache-Control: no-cache\r\n"
                                      "Connection: keep-alive\r\n"
                                      "X-Accel-Buffering: no\r\n"
                                      "\r\n";
    ::send(server_fd, sse_head.data(), sse_head.size(), 0);
    web::SSEWriter writer{server_fd};
    it->sse(req, writer);
    ::shutdown(server_fd, SHUT_WR);

    auto got = read_all(client_fd);
    REQUIRE(got.find("Content-Type: text/event-stream") != std::string::npos);
    REQUIRE(got.find("event: update\n") != std::string::npos);
    REQUIRE(got.find("data: tick\n") != std::string::npos);

    ::close(server_fd);
    ::close(client_fd);
}
