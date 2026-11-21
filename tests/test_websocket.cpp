#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <csignal>
#include <revet/web.hpp>
#include <revet/websocket.hpp>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <zlib.h>

namespace {

auto make_socketpair() -> std::pair<int, int> {
    std::array<int, 2> fds{};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) == 0);
    return {fds[0], fds[1]};
}

auto read_all(int fd, int timeout_ms = 200) -> std::string {
    std::string out;
    std::array<char, 4096> buf{};
    timeval tv{.tv_sec = 0, .tv_usec = timeout_ms * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (true) {
        const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n <= 0) {
            break;
        }
        out.append(buf.data(), static_cast<size_t>(n));
    }
    return out;
}

void send_raw(int fd, std::string_view bytes) {
    REQUIRE(::send(fd, bytes.data(), bytes.size(), 0) == static_cast<ssize_t>(bytes.size()));
}

// Client-to-server frame: always masked, with the given 4-byte key.
auto client_frame(uint8_t first_byte, std::string_view payload,
                  std::string_view key = std::string_view("\x01\x02\x03\x04", 4)) -> std::string {
    std::string out;
    out += static_cast<char>(first_byte);
    const size_t n = payload.size();
    if (n < 126) {
        out += static_cast<char>(0x80 | n);
    } else if (n <= 0xFFFF) {
        out += static_cast<char>(0x80 | 126);
        out += static_cast<char>((n >> 8) & 0xFF);
        out += static_cast<char>(n & 0xFF);
    } else {
        out += static_cast<char>(0x80 | 127);
        for (int i = 7; i >= 0; --i) {
            out += static_cast<char>((static_cast<uint64_t>(n) >> (i * 8)) & 0xFF);
        }
    }
    out.append(key);
    for (size_t i = 0; i < n; ++i) {
        out += static_cast<char>(payload[i] ^ key[i % 4]);
    }
    return out;
}

auto read_headers(int fd, int timeout_ms = 1000) -> std::string {
    std::string out;
    std::array<char, 512> buf{};
    timeval tv{.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (out.find("\r\n\r\n") == std::string::npos && out.size() < 65536) {
        const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
        if (n <= 0) {
            break;
        }
        out.append(buf.data(), static_cast<size_t>(n));
    }
    return out;
}

// RFC 6455 example key. Accept value is s3pPLMBiTxaQ9kYGzzhZRbK+xOo=.
auto ws_upgrade(std::string_view extra = {}) -> std::string {
    std::string req = "GET /ws HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                      "Sec-WebSocket-Version: 13\r\n";
    req += extra;
    req += "\r\n";
    return req;
}

struct ResetMw {
    ResetMw() {
        web::clear_middlewares();
    }
    ~ResetMw() {
        web::clear_middlewares();
    }
};

// server is the accepted socket (on_client owns it). fd is the peer.
struct Client {
    int server = -1;
    int fd = -1;
    Client() {
        std::signal(SIGPIPE, SIG_IGN);
        std::array<int, 2> fds{};
        REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) == 0);
        server = fds[0];
        fd = fds[1];
    }
    Client(const Client&) = delete;
    auto operator=(const Client&) -> Client& = delete;
    ~Client() {
        if (fd >= 0) {
            ::close(fd);
        }
        for (int i = 0; i < 400 && web::ws::Slot::in_use() != 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};

void wait_idle() {
    for (int i = 0; i < 400 && web::ws::Slot::in_use() != 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(web::ws::Slot::in_use() == 0);
}

void wait_flag(std::atomic<bool>& flag) {
    for (int i = 0; i < 400 && !flag.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace

TEST_CASE("accept_key matches RFC 6455 example") {
    REQUIRE(web::ws::accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("encode_frame: short text frame is unmasked") {
    REQUIRE(web::ws::encode_frame(web::ws::Opcode::Text, "Hello") == std::string("\x81\x05Hello", 7));
}

TEST_CASE("encode_frame: 16-bit length header for 126..65535 bytes") {
    const std::string payload(300, 'x');
    const auto frame = web::ws::encode_frame(web::ws::Opcode::Binary, payload);
    REQUIRE(frame.substr(0, 4) == std::string("\x82\x7E\x01\x2C", 4));
    REQUIRE(frame.size() == 4 + 300);
}

TEST_CASE("Conn::recv decodes a masked client text frame") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x81, "Hello"));

    auto conn = web::ws::Conn::make(server_fd);
    auto msg = conn->recv();
    REQUIRE(msg.has_value());
    REQUIRE_FALSE(msg->binary);
    REQUIRE(msg->data == "Hello");

    ::close(client_fd);
}

TEST_CASE("Conn::recv reassembles fragmented messages") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x02, "foo") + client_frame(0x00, "bar") + client_frame(0x80, "baz"));

    auto conn = web::ws::Conn::make(server_fd);
    auto msg = conn->recv();
    REQUIRE(msg.has_value());
    REQUIRE(msg->binary);
    REQUIRE(msg->data == "foobarbaz");

    ::close(client_fd);
}

TEST_CASE("Conn::recv answers ping with pong and keeps reading") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x89, "p") + client_frame(0x81, "after"));

    auto conn = web::ws::Conn::make(server_fd);
    auto msg = conn->recv();
    REQUIRE(msg.has_value());
    REQUIRE(msg->data == "after");
    REQUIRE(read_all(client_fd) == std::string("\x8A\x01p", 3));

    ::close(client_fd);
}

TEST_CASE("Conn::recv returns nullopt and echoes close when peer closes") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x88, std::string_view("\x03\xE8", 2)));

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_FALSE(conn->recv().has_value());
    REQUIRE_FALSE(conn->open());
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xE8", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv rejects unmasked client frame with close 1002") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, std::string_view("\x81\x05Hello", 7));

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xEA", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv rejects messages over the size cap with close 1009") {
    auto [server_fd, client_fd] = make_socketpair();
    // 64-bit length claiming 17 MiB; the cap check fires before any payload is read.
    std::string header("\x82\xFF\x00\x00\x00\x00\x01\x10\x00\x00\x00\x00\x01\x02\x03\x04", 16);
    send_raw(client_fd, header);

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xF1", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv rejects a one-byte close payload with close 1002") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x88, "x"));

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xEA", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv decodes a minimal 16-bit client frame") {
    auto [server_fd, client_fd] = make_socketpair();
    const std::string payload(200, 'q');
    send_raw(client_fd, client_frame(0x82, payload));

    auto conn = web::ws::Conn::make(server_fd);
    auto msg = conn->recv();
    REQUIRE(msg.has_value());
    REQUIRE(msg->binary);
    REQUIRE(msg->data == payload);

    ::close(client_fd);
}

TEST_CASE("Conn::recv decodes a minimal 64-bit client frame") {
    auto [server_fd, client_fd] = make_socketpair();
    const std::string payload(65536, 'z');
    const auto frame = client_frame(0x82, payload);
    std::thread sender([client_fd, &frame] -> void {
        size_t sent = 0;
        while (sent < frame.size()) {
            const ssize_t n = ::send(client_fd, frame.data() + sent, frame.size() - sent, 0);
            REQUIRE(n > 0);
            sent += static_cast<size_t>(n);
        }
    });

    auto conn = web::ws::Conn::make(server_fd);
    auto msg = conn->recv();
    sender.join();
    REQUIRE(msg.has_value());
    REQUIRE(msg->binary);
    REQUIRE(msg->data == payload);

    ::close(client_fd);
}

TEST_CASE("Conn::recv rejects a non-minimal 16-bit length with close 1002") {
    auto [server_fd, client_fd] = make_socketpair();
    // Masked text frame claiming a 16-bit length of 1.
    std::string frame("\x81\xFE\x00\x01\x01\x02\x03\x04", 8);
    frame += static_cast<char>('A' ^ 0x01);
    send_raw(client_fd, frame);

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xEA", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv rejects a non-minimal 64-bit length with close 1002") {
    auto [server_fd, client_fd] = make_socketpair();
    std::string frame("\x82\xFF\x00\x00\x00\x00\x00\x00\x00\x01", 10);
    frame += "\x01\x02\x03\x04";
    frame += static_cast<char>('A' ^ 0x01);
    send_raw(client_fd, frame);

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xEA", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv rejects a 64-bit length with the high bit set with close 1002") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, std::string("\x82\xFF\x80\x00\x00\x00\x00\x00\x00\x01\x01\x02\x03\x04", 14));

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xEA", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv echoes close 1012 and rejects close 1015") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x88, std::string_view("\x03\xF4", 2)));

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_FALSE(conn->recv().has_value());
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xF4", 4));

    ::close(client_fd);

    auto [server_fd2, client_fd2] = make_socketpair();
    send_raw(client_fd2, client_frame(0x88, std::string_view("\x03\xF7", 2)));
    auto conn2 = web::ws::Conn::make(server_fd2);
    REQUIRE_THROWS_AS(conn2->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd2) == std::string("\x88\x02\x03\xEA", 4));
    ::close(client_fd2);
}

TEST_CASE("Conn::recv rejects a reserved close code (1005) with close 1002") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x88, std::string_view("\x03\xED", 2)));

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xEA", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv rejects invalid UTF-8 in a text message with close 1007") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x81, "\xFF"));

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xEF", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv accepts valid multi-byte UTF-8 in text and binary messages") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x81, "caf\xC3\xA9") + client_frame(0x82, "\xFF\xFE"));

    auto conn = web::ws::Conn::make(server_fd);
    auto text = conn->recv();
    REQUIRE(text.has_value());
    REQUIRE(text->data == "caf\xC3\xA9");
    auto bin = conn->recv();
    REQUIRE(bin.has_value());
    REQUIRE(bin->binary);

    ::close(client_fd);
}

TEST_CASE("utf8_valid rejects overlongs, surrogates and truncated sequences") {
    REQUIRE(web::ws::detail::utf8_valid("plain ascii"));
    REQUIRE(web::ws::detail::utf8_valid("\xE2\x82\xAC"));
    REQUIRE_FALSE(web::ws::detail::utf8_valid("\xC0\xAF"));
    REQUIRE_FALSE(web::ws::detail::utf8_valid("\xED\xA0\x80"));
    REQUIRE_FALSE(web::ws::detail::utf8_valid("\xE2\x82"));
}

TEST_CASE("Conn::send_binary writes an unmasked frame and fails after close") {
    auto [server_fd, client_fd] = make_socketpair();
    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE(conn->send_binary("ab"));
    REQUIRE(read_all(client_fd) == std::string("\x82\x02"
                                               "ab",
                                               4));

    conn->close();
    REQUIRE_FALSE(conn->send_text("late"));

    ::close(client_fd);
}

TEST_CASE("Idle connection is pinged, then closed with 1001 if the peer never answers") {
    auto [server_fd, client_fd] = make_socketpair();
    auto conn = web::ws::Conn::make(server_fd, 50);
    REQUIRE_FALSE(conn->recv().has_value());
    REQUIRE(read_all(client_fd) == std::string("\x89\x00\x88\x02\x03\xE9", 6));

    ::close(client_fd);
}

TEST_CASE("Unsolicited pong is consumed without ending recv") {
    auto [server_fd, client_fd] = make_socketpair();
    auto conn = web::ws::Conn::make(server_fd, 50);
    send_raw(client_fd, client_frame(0x8A, "") + client_frame(0x81, "late"));
    auto msg = conn->recv();
    REQUIRE(msg.has_value());
    REQUIRE(msg->data == "late");

    ::close(client_fd);
}

TEST_CASE("weak handle from a Conn expires once the owner drops it") {
    auto [server_fd, client_fd] = make_socketpair();
    auto conn = web::ws::Conn::make(server_fd);
    std::weak_ptr<web::ws::Conn> weak = conn->weak_from_this();
    REQUIRE_FALSE(weak.expired());
    conn.reset();
    REQUIRE(weak.expired());

    ::close(client_fd);
}

TEST_CASE("Slot caps concurrent connections at k_max_connections") {
    std::vector<std::optional<web::ws::Slot>> held;
    for (int i = 0; i < web::ws::k_max_connections; ++i) {
        auto slot = web::ws::Slot::acquire();
        REQUIRE(slot.has_value());
        held.push_back(std::move(slot));
    }
    REQUIRE_FALSE(web::ws::Slot::acquire().has_value());
    held.pop_back();
    auto again = web::ws::Slot::acquire();
    REQUIRE(again.has_value());
    held.clear();
    again.reset();
    REQUIRE(web::ws::Slot::in_use() == 0);
}

TEST_CASE("ws_origin_allowed: no Origin or same-host Origin passes; cross-site and null fail") {
    web::Request req;
    req.headers.data["host"] = "example.com";
    REQUIRE(web::ws_origin_allowed(req));

    req.headers.data["origin"] = "https://example.com";
    REQUIRE(web::ws_origin_allowed(req));

    req.headers.data["origin"] = "https://evil.example";
    REQUIRE_FALSE(web::ws_origin_allowed(req));

    req.headers.data["origin"] = "null";
    REQUIRE_FALSE(web::ws_origin_allowed(req));

    for (const char* origin : {"https://example.com.evil", "https://evil.example.com", "https://example.com/path",
                               "https://user@example.com", "https://example.com:444", "http://example.com."}) {
        req.headers.data["origin"] = origin;
        REQUIRE_FALSE(web::ws_origin_allowed(req));
    }

    req.headers.data["host"] = "example.com:443";
    req.headers.data["origin"] = "https://example.com";
    REQUIRE_FALSE(web::ws_origin_allowed(req));
    req.headers.data["origin"] = "https://example.com:443";
    REQUIRE(web::ws_origin_allowed(req));
}

TEST_CASE("ws_origin_allowed ignores host case and scheme") {
    web::Request req;
    req.headers.data["host"] = "Example.COM";
    req.headers.data["origin"] = "http://example.com";
    REQUIRE(web::ws_origin_allowed(req));
}

TEST_CASE("Conn::recv reads bytes that arrived with the upgrade request first") {
    auto [server_fd, client_fd] = make_socketpair();
    auto conn = web::ws::Conn::make(server_fd);
    conn->prime(client_frame(0x81, "early"));
    auto msg = conn->recv();
    REQUIRE(msg.has_value());
    REQUIRE(msg->data == "early");

    ::close(client_fd);
}

TEST_CASE("valid_key requires base64 of exactly 16 bytes") {
    REQUIRE(web::ws::valid_key("dGhlIHNhbXBsZSBub25jZQ=="));
    REQUIRE_FALSE(web::ws::valid_key("k"));
    REQUIRE_FALSE(web::ws::valid_key("AAAA")); // 3 bytes
    REQUIRE_FALSE(web::ws::valid_key("!!!!!!!!!!!!!!!!!!!!!!!!"));
}

TEST_CASE("utf8_cut keeps incomplete trailing sequences for the next fragment") {
    REQUIRE(web::ws::detail::utf8_cut("ab") == 2);
    REQUIRE(web::ws::detail::utf8_cut("a\xC3") == 1);
    REQUIRE(web::ws::detail::utf8_cut("a\xE2\x82") == 1);
    REQUIRE(web::ws::detail::utf8_cut("a\xC3\xA9") == 3);
}

TEST_CASE("Conn::recv validates a codepoint split across text fragments") {
    auto [server_fd, client_fd] = make_socketpair();
    // "é" is C3 A9, split across two fragments
    send_raw(client_fd, client_frame(0x01, "caf\xC3") + client_frame(0x80, "\xA9"));

    auto conn = web::ws::Conn::make(server_fd);
    auto msg = conn->recv();
    REQUIRE(msg.has_value());
    REQUIRE(msg->data == "caf\xC3\xA9");

    ::close(client_fd);
}

TEST_CASE("Conn::recv closes 1007 when a text message ends mid-codepoint") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x01, "caf\xC3") + client_frame(0x80, "x"));

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xEF", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv fails 1007 as soon as a split codepoint turns invalid") {
    auto [server_fd, client_fd] = make_socketpair();
    // F4 90 would encode U+110000, above the Unicode range; the second fragment must fail the message at once.
    send_raw(client_fd, client_frame(0x01, "\xCE\xBA\xF4") + client_frame(0x80, "\x90"));

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xEF", 4));

    ::close(client_fd);
}

TEST_CASE("Conn::recv fails 1007 on a lead byte that can never start UTF-8") {
    auto [server_fd, client_fd] = make_socketpair();
    send_raw(client_fd, client_frame(0x01, "\xC0"));

    auto conn = web::ws::Conn::make(server_fd);
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xEF", 4));

    ::close(client_fd);
}

TEST_CASE("utf8_tail_ok accepts only prefixes that can still complete") {
    using web::ws::detail::utf8_tail_ok;
    REQUIRE(utf8_tail_ok(""));
    REQUIRE(utf8_tail_ok("\xC3"));
    REQUIRE(utf8_tail_ok("\xE0\xA0"));
    REQUIRE_FALSE(utf8_tail_ok("\xE0\x9F"));
    REQUIRE(utf8_tail_ok("\xED\x9F"));
    REQUIRE_FALSE(utf8_tail_ok("\xED\xA0"));
    REQUIRE(utf8_tail_ok("\xF4\x8F"));
    REQUIRE_FALSE(utf8_tail_ok("\xF4\x90"));
    REQUIRE_FALSE(utf8_tail_ok("\xC0"));
    REQUIRE_FALSE(utf8_tail_ok("\xF5"));
}

TEST_CASE("Conn::close from another thread wakes a blocked recv") {
    auto [server_fd, client_fd] = make_socketpair();
    auto conn = web::ws::Conn::make(server_fd, 0); // no idle timeout: recv blocks until woken
    std::thread closer([&conn] -> void {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        conn->close();
    });
    REQUIRE_FALSE(conn->recv().has_value());
    closer.join();

    ::close(client_fd);
}

TEST_CASE("is_ws_upgrade requires GET, Upgrade: websocket, Connection: upgrade and a valid key") {
    web::Request req;
    req.method = web::Method::GET;
    req.headers.data["upgrade"] = "WebSocket";
    req.headers.data["connection"] = "keep-alive, Upgrade";
    req.headers.data["sec-websocket-key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    REQUIRE(web::is_ws_upgrade(req));

    req.headers.data["connection"] = "keep-alive";
    REQUIRE_FALSE(web::is_ws_upgrade(req));

    req.headers.data["connection"] = "Upgrade";
    req.headers.data["sec-websocket-key"] = "k";
    REQUIRE_FALSE(web::is_ws_upgrade(req));

    req.headers.data["sec-websocket-key"] = "dGhlIHNhbXBsZSBub25jZQ==";
    req.method = web::Method::POST;
    REQUIRE_FALSE(web::is_ws_upgrade(req));
}

TEST_CASE("on_client completes a same-origin upgrade and echoes a coalesced frame") {
    ResetMw reset;
    std::atomic<bool> echoed{false};
    web::Route route;
    route.path = "/ws";
    route.ws = [&](const web::Request&, web::ws::Conn& conn) -> void {
        if (auto msg = conn.recv()) {
            conn.send_text(msg->data);
            echoed = msg->data == "hi";
        }
    };
    std::vector<web::Route> routes{std::move(route)};

    Client client;
    send_raw(client.fd, ws_upgrade("Origin: https://example.com\r\n") + client_frame(0x81, "hi"));
    web::on_client(client.server, routes);

    auto resp = read_headers(client.fd);
    if (resp.find("\x81\x02hi") == std::string::npos) {
        resp += read_all(client.fd, 1000);
    }
    REQUIRE(resp.find("HTTP/1.1 101 Switching Protocols") != std::string::npos);
    REQUIRE(resp.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    REQUIRE(resp.find("Upgrade: websocket") != std::string::npos);
    REQUIRE(resp.find("\x81\x02hi") != std::string::npos);
    wait_flag(echoed);
    REQUIRE(echoed);
    wait_idle();
}

TEST_CASE("on_client echoes a frame sent after the 101") {
    ResetMw reset;
    std::atomic<bool> echoed{false};
    web::Route route;
    route.path = "/ws";
    route.ws = [&](const web::Request&, web::ws::Conn& conn) -> void {
        if (auto msg = conn.recv()) {
            conn.send_text(msg->data);
            echoed = msg->data == "hi";
        }
    };
    std::vector<web::Route> routes{std::move(route)};

    Client client;
    send_raw(client.fd, ws_upgrade());
    web::on_client(client.server, routes);
    auto headers = read_headers(client.fd);
    REQUIRE(headers.find("HTTP/1.1 101") != std::string::npos);
    REQUIRE(headers.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    send_raw(client.fd, client_frame(0x81, "hi"));
    REQUIRE(read_all(client.fd, 1000).find("\x81\x02hi") != std::string::npos);
    wait_flag(echoed);
    REQUIRE(echoed);
    wait_idle();
}

TEST_CASE("on_client runs global middleware when the route has no guards") {
    ResetMw reset;
    std::atomic<int> hits{0};
    std::atomic<bool> ran{false};
    web::use([&](const web::Request&, const web::Next& next) -> web::Response {
        hits.fetch_add(1);
        return next();
    });
    web::Route route;
    route.path = "/ws";
    route.ws = [&](const web::Request&, web::ws::Conn&) -> void { ran = true; };
    std::vector<web::Route> routes{std::move(route)};

    Client client;
    send_raw(client.fd, ws_upgrade());
    web::on_client(client.server, routes);
    auto resp = read_headers(client.fd);
    REQUIRE(resp.find("HTTP/1.1 101") != std::string::npos);
    wait_flag(ran);
    REQUIRE(ran);
    REQUIRE(hits.load() == 1);
    wait_idle();
}

TEST_CASE("on_client refuses an upgrade when a guard does not call next") {
    ResetMw reset;
    std::atomic<bool> ran{false};
    web::Route route;
    route.path = "/ws";
    route.guards.emplace_back([](const web::Request&, const web::Next&) -> web::Response {
        return web::Response{.body = "nope", .status = web::status::forbidden};
    });
    route.ws = [&](const web::Request&, web::ws::Conn&) -> void { ran = true; };
    std::vector<web::Route> routes{std::move(route)};

    Client client;
    send_raw(client.fd, ws_upgrade());
    web::on_client(client.server, routes);
    auto resp = read_headers(client.fd);
    REQUIRE(resp.find("HTTP/1.1 403") != std::string::npos);
    REQUIRE(resp.find("nope") != std::string::npos);
    REQUIRE(resp.find("101") == std::string::npos);
    REQUIRE_FALSE(ran);
    wait_idle();
}

TEST_CASE("on_client turns a middleware exception into 500 and no 101") {
    ResetMw reset;
    web::use([](const web::Request&, const web::Next&) -> web::Response { throw std::runtime_error("boom"); });
    web::Route route;
    route.path = "/ws";
    route.ws = [](const web::Request&, web::ws::Conn&) -> void {};
    std::vector<web::Route> routes{std::move(route)};

    Client client;
    send_raw(client.fd, ws_upgrade());
    web::on_client(client.server, routes);
    auto resp = read_headers(client.fd);
    REQUIRE(resp.find("HTTP/1.1 500") != std::string::npos);
    REQUIRE(resp.find("101") == std::string::npos);
    wait_idle();
}

TEST_CASE("on_client rejects cross-origin and lookalike origins before 101") {
    ResetMw reset;
    std::atomic<int> ran{0};
    web::Route route;
    route.path = "/ws";
    route.ws = [&](const web::Request&, web::ws::Conn&) -> void { ran.fetch_add(1); };
    std::vector<web::Route> routes{std::move(route)};

    for (const char* origin :
         {"https://evil.example", "https://example.com.evil", "https://evil.example.com", "https://example.com/path",
          "https://user@example.com", "https://example.com:444", "null"}) {
        Client client;
        send_raw(client.fd, ws_upgrade(std::string("Origin: ") + origin + "\r\n"));
        web::on_client(client.server, routes);
        auto resp = read_headers(client.fd);
        REQUIRE(resp.find("HTTP/1.1 403") != std::string::npos);
        REQUIRE(resp.find("101") == std::string::npos);
    }
    REQUIRE(ran.load() == 0);
    wait_idle();
}

TEST_CASE("on_client rejects a bad version, HTTP/1.0, a missing Host, and Content-Length") {
    ResetMw reset;
    std::atomic<int> ran{0};
    web::Route route;
    route.path = "/ws";
    route.ws = [&](const web::Request&, web::ws::Conn&) -> void { ran.fetch_add(1); };
    std::vector<web::Route> routes{std::move(route)};

    {
        Client client;
        send_raw(client.fd, "GET /ws HTTP/1.1\r\nHost: example.com\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 12\r\n\r\n");
        web::on_client(client.server, routes);
        auto resp = read_headers(client.fd);
        REQUIRE(resp.find("HTTP/1.1 426") != std::string::npos);
        REQUIRE(resp.find("Sec-WebSocket-Version: 13") != std::string::npos);
        REQUIRE(resp.find("101") == std::string::npos);
    }
    {
        Client client;
        send_raw(client.fd, "GET /ws HTTP/1.0\r\nHost: example.com\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
        web::on_client(client.server, routes);
        auto resp = read_headers(client.fd);
        REQUIRE(resp.find("HTTP/1.1 400") != std::string::npos);
        REQUIRE(resp.find("101") == std::string::npos);
    }
    {
        Client client;
        send_raw(client.fd, "GET /ws HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
        web::on_client(client.server, routes);
        auto resp = read_headers(client.fd);
        REQUIRE(resp.find("HTTP/1.1 400") != std::string::npos);
        REQUIRE(resp.find("101") == std::string::npos);
    }
    {
        Client client;
        send_raw(client.fd, ws_upgrade("Content-Length: 2\r\n") + client_frame(0x81, "hi"));
        web::on_client(client.server, routes);
        auto resp = read_headers(client.fd);
        REQUIRE(resp.find("HTTP/1.1 400") != std::string::npos);
        REQUIRE(resp.find("101") == std::string::npos);
        REQUIRE(resp.find("\x81\x02hi") == std::string::npos);
    }
    REQUIRE(ran.load() == 0);
    wait_idle();
}

TEST_CASE("on_client returns 503 when the connection cap is full") {
    ResetMw reset;
    Client client;
    std::vector<std::optional<web::ws::Slot>> held;
    for (int i = 0; i < web::ws::k_max_connections; ++i) {
        auto slot = web::ws::Slot::acquire();
        REQUIRE(slot.has_value());
        held.push_back(std::move(slot));
    }
    web::Route route;
    route.path = "/ws";
    route.ws = [](const web::Request&, web::ws::Conn&) -> void {};
    std::vector<web::Route> routes{std::move(route)};

    send_raw(client.fd, ws_upgrade());
    web::on_client(client.server, routes);
    auto resp = read_headers(client.fd);
    REQUIRE(resp.find("HTTP/1.1 503") != std::string::npos);
    REQUIRE(resp.find("101") == std::string::npos);
    held.clear();
    wait_idle();
}

TEST_CASE("on_client serves an ordinary GET and a 413 without upgrading") {
    ResetMw reset;
    web::Route hello;
    hello.path = "/hello";
    hello.get = [](const web::Request&) -> web::Response { return web::to_response("world"); };
    web::Route upload;
    upload.path = "/up";
    std::atomic<bool> posted{false};
    upload.post = [&](const web::Request&) -> web::Response {
        posted = true;
        return web::to_response("no");
    };
    std::vector<web::Route> routes{std::move(hello), std::move(upload)};

    {
        Client client;
        send_raw(client.fd, "GET /hello HTTP/1.1\r\nHost: example.com\r\n\r\n");
        web::on_client(client.server, routes);
        auto resp = read_headers(client.fd);
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
        REQUIRE(resp.find("world") != std::string::npos);
    }
    {
        const auto prev = web::config::max_body_bytes();
        web::config::max_body_bytes(4);
        Client client;
        send_raw(client.fd, "POST /up HTTP/1.1\r\nHost: example.com\r\nContent-Type: application/json\r\n"
                            "Content-Length: 100\r\n\r\n{\"a\":1}");
        web::on_client(client.server, routes);
        auto resp = read_headers(client.fd);
        web::config::max_body_bytes(prev);
        REQUIRE(resp.find("HTTP/1.1 413") != std::string::npos);
        REQUIRE_FALSE(posted);
    }
}

TEST_CASE("a throwing WebSocket handler still releases its slot") {
    ResetMw reset;
    web::Route route;
    route.path = "/ws";
    route.ws = [](const web::Request&, web::ws::Conn&) -> void { throw std::runtime_error("handler"); };
    std::vector<web::Route> routes{std::move(route)};

    Client client;
    send_raw(client.fd, ws_upgrade());
    web::on_client(client.server, routes);
    auto resp = read_headers(client.fd);
    REQUIRE(resp.find("HTTP/1.1 101") != std::string::npos);
    wait_idle();
}

TEST_CASE("negotiate_deflate accepts permessage-deflate and echoes the takeover and window params it honors") {
    using web::ws::negotiate_deflate;
    auto plain = negotiate_deflate("permessage-deflate");
    REQUIRE(plain.enabled);
    REQUIRE(plain.response == "permessage-deflate");

    auto no_ctx = negotiate_deflate("permessage-deflate; server_no_context_takeover; client_no_context_takeover");
    REQUIRE(no_ctx.enabled);
    REQUIRE(no_ctx.server_no_ctx);
    REQUIRE(no_ctx.response == "permessage-deflate; server_no_context_takeover");

    auto window = negotiate_deflate("permessage-deflate; server_max_window_bits=10");
    REQUIRE(window.enabled);
    REQUIRE(window.server_bits == 10);
    REQUIRE(window.response == "permessage-deflate; server_max_window_bits=10");
}

TEST_CASE("negotiate_deflate declines offers it cannot honor and falls through to the next") {
    using web::ws::negotiate_deflate;
    REQUIRE_FALSE(negotiate_deflate("").enabled);
    REQUIRE_FALSE(negotiate_deflate("x-webkit-deflate-frame").enabled);
    REQUIRE_FALSE(negotiate_deflate("permessage-deflate; server_max_window_bits=8").enabled);
    REQUIRE_FALSE(negotiate_deflate("permessage-deflate; unknown_param").enabled);
    REQUIRE_FALSE(negotiate_deflate("permessage-deflate; server_no_context_takeover=1").enabled);

    auto next = negotiate_deflate("permessage-deflate; server_max_window_bits=8, permessage-deflate");
    REQUIRE(next.enabled);
    REQUIRE(next.response == "permessage-deflate");
}

TEST_CASE("Conn::recv inflates a permessage-deflate text message and send_text deflates the reply") {
    auto [server_fd, client_fd] = make_socketpair();
    auto conn = web::ws::Conn::make(server_fd);
    conn->set_deflate(web::ws::negotiate_deflate("permessage-deflate"));

    // Raw deflate of "Hello", sync-flushed, with the 00 00 FF FF tail removed as RFC 7692 requires.
    z_stream zs{};
    REQUIRE(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::string plain = "Hello";
    std::string out(64, '\0');
    zs.next_in = reinterpret_cast<Bytef*>(plain.data());
    zs.avail_in = static_cast<uInt>(plain.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());
    deflate(&zs, Z_SYNC_FLUSH);
    out.resize(out.size() - zs.avail_out - 4);
    deflateEnd(&zs);

    // RSV1 set, FIN set, text, masked, 5 bytes.
    std::string frame = std::string("\xC1\x80", 2) + std::string("\x01\x02\x03\x04", 4);
    for (size_t i = 0; i < out.size(); ++i) {
        frame += static_cast<char>(out[i] ^ "\x01\x02\x03\x04"[i % 4]);
    }
    frame[1] = static_cast<char>(0x80 | out.size());
    send_raw(client_fd, frame);

    auto msg = conn->recv();
    REQUIRE(msg.has_value());
    REQUIRE_FALSE(msg->binary);
    REQUIRE(msg->data == "Hello");

    REQUIRE(conn->send_text("Hello"));
    const std::string reply = read_all(client_fd);
    REQUIRE(reply.size() >= 2);
    REQUIRE(static_cast<uint8_t>(reply[0]) == 0xC1); // FIN + RSV1 + text

    ::close(client_fd);
}

namespace {

// RFC 7692 §7.2.1: raw deflate, sync-flushed, with the 00 00 FF FF tail removed.
auto deflate_message(std::string_view plain) -> std::string {
    z_stream zs{};
    REQUIRE(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::string out(256, '\0');
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(plain.data()));
    zs.avail_in = static_cast<uInt>(plain.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());
    REQUIRE(deflate(&zs, Z_SYNC_FLUSH) == Z_OK);
    out.resize(out.size() - zs.avail_out);
    deflateEnd(&zs);
    REQUIRE(out.size() >= 4);
    out.resize(out.size() - 4);
    return out;
}

// Inflates one message: the stripped tail is restored before the final flush.
auto inflate_message(std::string_view packed) -> std::string {
    z_stream zs{};
    REQUIRE(inflateInit2(&zs, -15) == Z_OK);
    std::string input(packed);
    input += std::string("\0\0\xff\xff", 4);
    std::string out(256, '\0');
    zs.next_in = reinterpret_cast<Bytef*>(input.data());
    zs.avail_in = static_cast<uInt>(input.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());
    REQUIRE(inflate(&zs, Z_SYNC_FLUSH) == Z_OK);
    out.resize(out.size() - zs.avail_out);
    inflateEnd(&zs);
    return out;
}

} // namespace

TEST_CASE("on_client negotiates permessage-deflate and round-trips a compressed message") {
    ResetMw reset;
    std::atomic<bool> echoed{false};
    web::Route route;
    route.path = "/ws";
    route.ws = [&](const web::Request&, web::ws::Conn& conn) -> void {
        if (auto msg = conn.recv()) {
            echoed = msg->data == "hello";
            conn.send_text(msg->data);
        }
    };
    std::vector<web::Route> routes{std::move(route)};

    // RSV1 set: a compressed text frame, masked as clients must send it.
    const std::string packed = deflate_message("hello");
    Client client;
    send_raw(client.fd, ws_upgrade("Sec-WebSocket-Extensions: permessage-deflate\r\n") + client_frame(0xC1, packed));
    web::on_client(client.server, routes);

    std::string resp = read_headers(client.fd);
    REQUIRE(resp.find("HTTP/1.1 101 Switching Protocols") != std::string::npos);
    REQUIRE(resp.find("Sec-WebSocket-Extensions: permessage-deflate") != std::string::npos);

    // The reply frame may have arrived in the same read as the headers.
    std::string body = resp.substr(resp.find("\r\n\r\n") + 4);
    if (body.size() < 2) {
        body += read_all(client.fd, 1000);
    }
    REQUIRE(body.size() >= 2);
    REQUIRE(static_cast<uint8_t>(body[0]) == 0xC1); // FIN + RSV1 + text, unmasked
    const size_t len = static_cast<uint8_t>(body[1]) & 0x7F;
    REQUIRE(body.size() >= 2 + len);
    REQUIRE(inflate_message(body.substr(2, len)) == "hello");

    wait_flag(echoed);
    REQUIRE(echoed);
    wait_idle();
}

TEST_CASE("Conn::recv closes 1013 when a message would exceed the process-wide buffer budget") {
    // Holds the budget full so that any new reservation is refused.
    auto& total = web::ws::detail::buffered_total();
    struct Fill {
        std::atomic<size_t>& total;
        size_t n{web::ws::k_max_buffered - 100};
        explicit Fill(std::atomic<size_t>& t) : total(t) {
            total.fetch_add(n);
        }
        ~Fill() {
            total.fetch_sub(n);
        }
    } fill(total);

    auto [server_fd, client_fd] = make_socketpair();
    auto conn = web::ws::Conn::make(server_fd);
    send_raw(client_fd, client_frame(0x81, std::string(150, 'a'))); // 150 > the 100 bytes of room left
    REQUIRE_THROWS_AS(conn->recv(), web::ws::Error);
    REQUIRE(read_all(client_fd) == std::string("\x88\x02\x03\xF5", 4)); // 1013

    ::close(client_fd);
}
