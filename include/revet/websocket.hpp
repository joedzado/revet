#pragma once

#include <sodium.h>

#include <array>
#include <atomic>
#include <chrono>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <format>
#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <utility>

// RFC 6455 WebSocket: handshake, frame codec, and a server-side connection.

namespace web::ws {

inline constexpr std::string_view k_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
inline constexpr size_t k_max_message = size_t{1} << 20; // 1 MiB per reassembled message
inline constexpr int k_idle_timeout_ms = 30'000;          // ping after this much silence
inline constexpr int k_max_connections = 256;             // process-wide cap on open sockets
inline constexpr int k_send_timeout_ms = 5'000;           // a stalled peer can't hold the write lock forever
inline constexpr auto k_frame_deadline = std::chrono::seconds(10); // bounds a slow-drip frame, not just one recv

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

enum class CloseCode : uint16_t {
    Normal = 1000,
    GoingAway = 1001,
    Protocol = 1002,
    InvalidData = 1007,
    TooBig = 1009,
};

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Message {
    bool binary = false;
    std::string data;
};

namespace detail {

inline auto sha1(std::string_view input) -> std::array<uint8_t, 20> {
    std::array<uint32_t, 5> h{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

    std::string msg(input);
    const uint64_t bits = static_cast<uint64_t>(input.size()) * 8;
    msg += static_cast<char>(0x80);
    while (msg.size() % 64 != 56) {
        msg += '\0';
    }
    for (int i = 7; i >= 0; --i) {
        msg += static_cast<char>((bits >> (i * 8)) & 0xFF);
    }

    for (size_t off = 0; off < msg.size(); off += 64) {
        std::array<uint32_t, 80> w{};
        for (size_t i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const uint8_t*>(msg.data() + off + (i * 4));
            w[i] = (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
        }
        for (size_t i = 16; i < 80; ++i) {
            w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h[0];
        uint32_t b = h[1];
        uint32_t c = h[2];
        uint32_t d = h[3];
        uint32_t e = h[4];
        for (size_t i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const uint32_t t = std::rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = std::rotl(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    std::array<uint8_t, 20> out{};
    for (size_t i = 0; i < 5; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            out[(i * 4) + j] = static_cast<uint8_t>(h[i] >> (24 - (j * 8)));
        }
    }
    return out;
}

// Strict UTF-8 (RFC 3629): no overlongs, no surrogates, nothing above U+10FFFF.
inline auto utf8_valid(std::string_view s) -> bool {
    size_t i = 0;
    while (i < s.size()) {
        const auto lead = static_cast<uint8_t>(s[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        size_t extra = 0;
        uint32_t cp = 0;
        uint32_t min = 0;
        if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            cp = lead & 0x1F;
            min = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            cp = lead & 0x0F;
            min = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            cp = lead & 0x07;
            min = 0x10000;
        } else {
            return false;
        }
        if (i + extra >= s.size()) {
            return false;
        }
        for (size_t k = 1; k <= extra; ++k) {
            const auto cont = static_cast<uint8_t>(s[i + k]);
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (cont & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

// Length of the trailing, possibly incomplete UTF-8 sequence in s; bytes before it can be validated now.
inline auto utf8_cut(std::string_view s) -> size_t {
    for (size_t back = 1; back <= 3 && back <= s.size(); ++back) {
        const auto b = static_cast<uint8_t>(s[s.size() - back]);
        if ((b & 0xC0) == 0x80) {
            continue;
        }
        size_t need = 1;
        if ((b & 0xE0) == 0xC0) {
            need = 2;
        } else if ((b & 0xF0) == 0xE0) {
            need = 3;
        } else if ((b & 0xF8) == 0xF0) {
            need = 4;
        }
        return back < need ? s.size() - back : s.size();
    }
    return s.size();
}

// Codes a peer may send. 1004, 1005, 1006 and 1015 are reserved and never on the wire.
// 1012–1014 are registry additions after RFC 6455.
inline auto close_code_valid(uint16_t code) -> bool {
    return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999);
}

} // namespace detail

// Sec-WebSocket-Accept value for a client's Sec-WebSocket-Key.
inline auto accept_key(std::string_view client_key) -> std::string {
    const std::string joined = std::string(client_key) + std::string(k_guid);
    const auto digest = detail::sha1(joined);
    std::string out(sodium_base64_ENCODED_LEN(digest.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(out.data(), out.size(), digest.data(), digest.size(), sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}

// Sec-WebSocket-Key must be base64 of exactly 16 random bytes (RFC 6455 §4.1).
inline auto valid_key(std::string_view key) -> bool {
    std::array<unsigned char, 16> raw{};
    size_t raw_len = 0;
    return sodium_base642bin(raw.data(), raw.size(), key.data(), key.size(), nullptr, &raw_len, nullptr,
                             sodium_base64_VARIANT_ORIGINAL) == 0 &&
           raw_len == raw.size();
}

// Server-to-client frame: unmasked, single fragment unless fin is false.
inline auto encode_frame(Opcode op, std::string_view payload, bool fin = true) -> std::string {
    std::string out;
    out.reserve(payload.size() + 10);
    out += static_cast<char>((fin ? 0x80 : 0x00) | static_cast<uint8_t>(op));
    const size_t n = payload.size();
    if (n < 126) {
        out += static_cast<char>(n);
    } else if (n <= 0xFFFF) {
        out += static_cast<char>(126);
        out += static_cast<char>((n >> 8) & 0xFF);
        out += static_cast<char>(n & 0xFF);
    } else {
        out += static_cast<char>(127);
        for (int i = 7; i >= 0; --i) {
            out += static_cast<char>((static_cast<uint64_t>(n) >> (i * 8)) & 0xFF);
        }
    }
    out += payload;
    return out;
}

// Counts open WebSocket connections process-wide; holds one slot for its lifetime.
class Slot {
public:
    [[nodiscard]] static auto acquire() -> std::optional<Slot> {
        auto& live = counter();
        if (live.fetch_add(1) >= k_max_connections) {
            live.fetch_sub(1);
            return std::nullopt;
        }
        return Slot{};
    }
    Slot(Slot&& other) noexcept : owns_(std::exchange(other.owns_, false)) {}
    auto operator=(Slot&&) -> Slot& = delete;
    Slot(const Slot&) = delete;
    auto operator=(const Slot&) -> Slot& = delete;
    ~Slot() {
        if (owns_) {
            counter().fetch_sub(1);
        }
    }

    [[nodiscard]] static auto in_use() -> int { return counter().load(); }

private:
    Slot() = default;
    static auto counter() -> std::atomic<int>& {
        static std::atomic<int> live{0};
        return live;
    }
    bool owns_ = true;
};

// Server-side connection. Owns the socket: it closes when the last shared_ptr goes away.
// The handler's Conn& is valid only while the handler runs, on its thread. Other threads
// keep a weak_from_this() handle and lock it for each send; writes are serialized.
class Conn : public std::enable_shared_from_this<Conn> {
public:
    // Takes a slot itself; nullptr when the process is at k_max_connections.
    [[nodiscard]] static auto make(int fd, int idle_timeout_ms = k_idle_timeout_ms) -> std::shared_ptr<Conn> {
        auto slot = Slot::acquire();
        if (!slot) {
            return nullptr;
        }
        return make(fd, std::move(*slot), idle_timeout_ms);
    }
    // The slot is released when the connection's fd is closed, however many references outlive the handler.
    [[nodiscard]] static auto make(int fd, Slot slot, int idle_timeout_ms = k_idle_timeout_ms)
        -> std::shared_ptr<Conn> {
        if (idle_timeout_ms > 0) {
            timeval tv{idle_timeout_ms / 1000, (idle_timeout_ms % 1000) * 1000};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        timeval send_tv{k_send_timeout_ms / 1000, (k_send_timeout_ms % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv));
        return std::shared_ptr<Conn>(new Conn(fd, std::move(slot)));
    }
    ~Conn() { ::close(fd_); }
    Conn(const Conn&) = delete;
    auto operator=(const Conn&) -> Conn& = delete;
    Conn(Conn&&) = delete;
    auto operator=(Conn&&) -> Conn& = delete;

    [[nodiscard]] auto open() const -> bool { return open_.load(); }

    // Bytes that arrived after the upgrade request's headers; consumed before the socket is read.
    void prime(std::string bytes) { inbuf_ = std::move(bytes); }

    // Next data message, or nullopt once the peer closes or stops answering pings.
    // Throws Error after sending a close frame on protocol violations.
    auto recv() -> std::optional<Message> {
        Message msg;
        bool in_fragment = false;
        bool ping_outstanding = false;
        std::string utf8_pending; // trailing bytes of a text fragment split mid-codepoint
        while (open_.load()) {
            IoStatus st = IoStatus::Ok;
            auto frame = read_frame(st);
            if (st == IoStatus::Timeout) {
                if (ping_outstanding) {
                    close(CloseCode::GoingAway);
                    return std::nullopt;
                }
                if (!write(Opcode::Ping, "")) {
                    return std::nullopt;
                }
                ping_outstanding = true;
                continue;
            }
            if (!frame) {
                open_ = false;
                return std::nullopt;
            }
            ping_outstanding = false;

            const auto op = frame->op;
            if (op == Opcode::Ping) {
                write(Opcode::Pong, frame->payload);
                continue;
            }
            if (op == Opcode::Pong) {
                continue;
            }
            if (op == Opcode::Close) {
                validate_close_payload(frame->payload);
                write(Opcode::Close, frame->payload);
                open_ = false;
                ::shutdown(fd_, SHUT_RDWR);
                return std::nullopt;
            }
            if (op == Opcode::Continuation) {
                if (!in_fragment) {
                    fail(CloseCode::Protocol, "unexpected continuation frame");
                }
            } else {
                if (in_fragment) {
                    fail(CloseCode::Protocol, "data frame inside fragmented message");
                }
                msg.binary = op == Opcode::Binary;
                in_fragment = !frame->fin;
            }
            if (msg.data.size() + frame->payload.size() > k_max_message) {
                fail(CloseCode::TooBig, "message too large");
            }
            msg.data += frame->payload;
            if (!msg.binary) {
                // Validate each fragment as it arrives; only a split codepoint carries over.
                std::string chunk = utf8_pending + frame->payload;
                const size_t cut = detail::utf8_cut(chunk);
                if (!detail::utf8_valid(std::string_view(chunk).substr(0, cut))) {
                    fail(CloseCode::InvalidData, "invalid UTF-8 in text message");
                }
                utf8_pending = chunk.substr(cut);
            }
            if (frame->fin) {
                if (!utf8_pending.empty()) {
                    fail(CloseCode::InvalidData, "truncated UTF-8 in text message");
                }
                return msg;
            }
        }
        return std::nullopt;
    }

    // Returns false if the connection is closed or the write failed.
    auto send_text(std::string_view text) -> bool { return write(Opcode::Text, text); }
    auto send_binary(std::string_view bytes) -> bool { return write(Opcode::Binary, bytes); }

    void close(CloseCode code = CloseCode::Normal) {
        const auto raw = static_cast<uint16_t>(code);
        const std::string payload{static_cast<char>(raw >> 8), static_cast<char>(raw & 0xFF)};
        std::lock_guard lock(write_mu_);
        if (!open_.exchange(false)) {
            return;
        }
        send_all(encode_frame(Opcode::Close, payload));
        // Wakes a recv() blocked on another thread; the handler then sees nullopt.
        ::shutdown(fd_, SHUT_RDWR);
    }

private:
    Conn(int fd, Slot slot) : fd_(fd), slot_(std::move(slot)) {}

    enum class IoStatus { Ok, Closed, Timeout };

    struct Frame {
        bool fin = false;
        Opcode op = Opcode::Continuation;
        std::string payload;
    };

    [[noreturn]] void fail(CloseCode code, const char* why) {
        close(code);
        throw Error(why);
    }

    void validate_close_payload(const std::string& payload) {
        if (payload.empty()) {
            return;
        }
        if (payload.size() == 1) {
            fail(CloseCode::Protocol, "close payload of one byte");
        }
        const auto code = static_cast<uint16_t>((static_cast<uint8_t>(payload[0]) << 8) |
                                                static_cast<uint8_t>(payload[1]));
        if (!detail::close_code_valid(code)) {
            fail(CloseCode::Protocol, "invalid close code");
        }
        if (payload.size() > 2 && !detail::utf8_valid(std::string_view(payload).substr(2))) {
            fail(CloseCode::InvalidData, "invalid UTF-8 in close reason");
        }
    }

    // Reads exactly n bytes. A timeout before any byte arrives reports Timeout; one mid-read is a close.
    auto read_exact(char* buf, size_t n, IoStatus& st) -> bool {
        size_t got = 0;
        if (!inbuf_.empty()) {
            const size_t take = std::min(n, inbuf_.size());
            std::memcpy(buf, inbuf_.data(), take);
            inbuf_.erase(0, take);
            got = take;
        }
        while (got < n) {
            const ssize_t r = ::recv(fd_, buf + got, n - got, 0);
            if (r > 0) {
                got += static_cast<size_t>(r);
                const auto now = std::chrono::steady_clock::now();
                if (frame_deadline_ == std::chrono::steady_clock::time_point::max()) {
                    frame_deadline_ = now + k_frame_deadline; // armed by the frame's first byte, not by idle time
                }
                if (now > frame_deadline_) {
                    st = IoStatus::Closed;
                    return false;
                }
                continue;
            }
            if (r < 0 && errno == EINTR) {
                continue;
            }
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && got == 0 && n > 0 && st == IoStatus::Ok) {
                st = IoStatus::Timeout;
                return false;
            }
            st = IoStatus::Closed;
            return false;
        }
        return true;
    }

    auto read_frame(IoStatus& st) -> std::optional<Frame> {
        frame_deadline_ = std::chrono::steady_clock::time_point::max();
        std::array<char, 2> head{};
        if (!read_exact(head.data(), head.size(), st)) {
            return std::nullopt;
        }
        const auto b0 = static_cast<uint8_t>(head[0]);
        const auto b1 = static_cast<uint8_t>(head[1]);
        if ((b0 & 0x70) != 0) {
            fail(CloseCode::Protocol, "reserved bits set");
        }
        if ((b1 & 0x80) == 0) {
            fail(CloseCode::Protocol, "client frame not masked");
        }

        const uint8_t raw_op = b0 & 0x0F;
        if ((raw_op >= 0x3 && raw_op <= 0x7) || raw_op >= 0xB) {
            fail(CloseCode::Protocol, "reserved opcode");
        }
        Frame frame;
        frame.fin = (b0 & 0x80) != 0;
        frame.op = static_cast<Opcode>(raw_op);
        const bool control = (raw_op & 0x08) != 0;

        // RFC 6455 §5.2: the smallest length encoding is required, and the 64-bit high bit must be 0.
        const uint8_t len_marker = b1 & 0x7F;
        uint64_t len = len_marker;
        if (len_marker == 126) {
            std::array<char, 2> ext{};
            if (!read_exact(ext.data(), ext.size(), st)) {
                return fail_mid_frame(st);
            }
            len = (static_cast<uint64_t>(static_cast<uint8_t>(ext[0])) << 8) | static_cast<uint8_t>(ext[1]);
            if (len < 126) {
                fail(CloseCode::Protocol, "non-minimal payload length");
            }
        } else if (len_marker == 127) {
            std::array<char, 8> ext{};
            if (!read_exact(ext.data(), ext.size(), st)) {
                return fail_mid_frame(st);
            }
            if ((static_cast<uint8_t>(ext[0]) & 0x80) != 0) {
                fail(CloseCode::Protocol, "payload length high bit set");
            }
            len = 0;
            for (char c : ext) {
                len = (len << 8) | static_cast<uint8_t>(c);
            }
            if (len <= 0xFFFF) {
                fail(CloseCode::Protocol, "non-minimal payload length");
            }
        }
        if (control && (!frame.fin || len > 125)) {
            fail(CloseCode::Protocol, "invalid control frame");
        }
        if (len > k_max_message) {
            fail(CloseCode::TooBig, "frame too large");
        }

        std::array<char, 4> mask{};
        if (!read_exact(mask.data(), mask.size(), st)) {
            return fail_mid_frame(st);
        }
        frame.payload.resize(len);
        if (len > 0 && !read_exact(frame.payload.data(), len, st)) {
            return fail_mid_frame(st);
        }
        for (size_t i = 0; i < frame.payload.size(); ++i) {
            frame.payload[i] = static_cast<char>(frame.payload[i] ^ mask[i % 4]);
        }
        return frame;
    }

    // A timeout after the frame has started is a dead peer, not an idle one.
    static auto fail_mid_frame(IoStatus& st) -> std::optional<Frame> {
        if (st == IoStatus::Timeout) {
            st = IoStatus::Closed;
        }
        return std::nullopt;
    }

    auto write(Opcode op, std::string_view payload) -> bool {
        std::lock_guard lock(write_mu_);
        if (!open_.load()) {
            return false;
        }
        return send_all(encode_frame(op, payload));
    }

    // Caller holds write_mu_.
    auto send_all(const std::string& bytes) -> bool {
        size_t sent = 0;
        while (sent < bytes.size()) {
            const ssize_t r = ::send(fd_, bytes.data() + sent, bytes.size() - sent, 0);
            if (r < 0 && errno == EINTR) {
                continue;
            }
            if (r <= 0) {
                // A partial frame can't be resumed; shutting down lets the peer see EOF, not garbage.
                open_ = false;
                ::shutdown(fd_, SHUT_RDWR);
                return false;
            }
            sent += static_cast<size_t>(r);
        }
        return true;
    }

    int fd_;
    Slot slot_;
    std::string inbuf_; // bytes read with the upgrade request, served before the socket
    std::chrono::steady_clock::time_point frame_deadline_{};
    std::atomic<bool> open_{true};
    std::mutex write_mu_;
};

} // namespace web::ws
