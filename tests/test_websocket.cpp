#include <catch2/catch_test_macros.hpp>
#include <revet/web.hpp>
#include <revet/websocket.hpp>
#include <chrono>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

namespace {

auto make_socketpair() -> std::pair<int, int> {
    std::array<int, 2> fds{};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) == 0);
    return {fds[0], fds[1]};
}

auto read_all(int fd, int timeout_ms = 200) -> std::string {
    std::string out;
    std::array<char, 4096> buf{};
    timeval tv{0, timeout_ms * 1000};
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
auto client_frame(uint8_t first_byte, std::string_view payload, std::string_view key = std::string_view("\x01\x02\x03\x04", 4))
    -> std::string {
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
    // 64-bit length claiming 2 MiB; the cap check fires before any payload is read.
    std::string header("\x82\xFF\x00\x00\x00\x00\x00\x20\x00\x00\x00\x00\x01\x02\x03\x04", 16);
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
    std::thread sender([client_fd, &frame] {
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
    REQUIRE(read_all(client_fd) == std::string("\x82\x02" "ab", 4));

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
    REQUIRE_FALSE(web::ws::valid_key("AAAA"));          // 3 bytes
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

TEST_CASE("Conn::close from another thread wakes a blocked recv") {
    auto [server_fd, client_fd] = make_socketpair();
    auto conn = web::ws::Conn::make(server_fd, 0); // no idle timeout: recv blocks until woken
    std::thread closer([&conn] {
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
