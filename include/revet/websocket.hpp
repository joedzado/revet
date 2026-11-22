#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <sodium.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <utility>
#include <zlib.h>

#include "c_api.hpp"
#include "hash.hpp"

// RFC 6455 WebSocket: handshake, frame codec, and a server-side connection.

namespace web::ws {

inline constexpr std::string_view k_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
inline constexpr size_t k_max_message = size_t{1} << 24;           // 16 MiB per reassembled message
inline constexpr int k_idle_timeout_ms = 30'000;                   // ping after this much silence
inline constexpr int k_max_connections = 256;                      // process-wide cap on open sockets
inline constexpr int k_send_timeout_ms = 5'000;                    // a stalled peer can't hold the write lock forever
inline constexpr auto k_frame_deadline = std::chrono::seconds(10); // bounds a slow-drip frame, not just one recv
inline constexpr size_t k_max_buffered = size_t{256} << 20;        // bytes held by in-flight messages, process-wide

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
    TryAgainLater = 1013,
};

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Message {
    bool binary = false;
    std::string data;
};

namespace detail {

// Bytes held by in-flight messages across all connections. A message holds what it has reassembled so far.
inline auto buffered_total() -> std::atomic<size_t>& {
    static std::atomic<size_t> total{0};
    return total;
}

// One message's share of buffered_total(); released when the message is returned or abandoned.
class Budget {
public:
    Budget() = default;
    Budget(const Budget&) = delete;
    auto operator=(const Budget&) -> Budget& = delete;
    Budget(Budget&&) = delete;
    auto operator=(Budget&&) -> Budget& = delete;
    ~Budget() {
        buffered_total().fetch_sub(held_);
    }

    [[nodiscard]] auto reserve(size_t n) -> bool {
        if (buffered_total().fetch_add(n) + n > k_max_buffered) {
            buffered_total().fetch_sub(n);
            return false;
        }
        held_ += n;
        return true;
    }

private:
    size_t held_ = 0;
};

// Strict UTF-8 (RFC 3629): no overlongs, no surrogates, nothing above U+10FFFF.
inline auto utf8_valid(std::string_view s) -> bool {
    size_t i = 0;
    while (i < s.size()) {
        const auto lead = static_cast<uint8_t>(s.at(i));
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
            const auto cont = static_cast<uint8_t>(s.at(i + k));
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6U) | (cont & 0x3FU);
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
        const auto b = static_cast<uint8_t>(s.at(s.size() - back));
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

// True if an incomplete trailing sequence can still become valid UTF-8 (RFC 3629 table 3-7).
// Rejects a bad lead byte or an out-of-range second byte now, instead of at the end of the message.
inline auto utf8_tail_ok(std::string_view tail) -> bool {
    if (tail.empty()) {
        return true;
    }
    const auto lead = static_cast<uint8_t>(tail.at(0));
    uint8_t lo2 = 0x80;
    uint8_t hi2 = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
    } else if (lead == 0xE0) {
        lo2 = 0xA0;
    } else if (lead >= 0xE1 && lead <= 0xEC) {
    } else if (lead == 0xED) {
        hi2 = 0x9F;
    } else if (lead >= 0xEE && lead <= 0xEF) {
    } else if (lead == 0xF0) {
        lo2 = 0x90;
    } else if (lead >= 0xF1 && lead <= 0xF3) {
    } else if (lead == 0xF4) {
        hi2 = 0x8F;
    } else {
        return false;
    }
    // The second byte has the lead's range; later ones are plain continuation bytes.
    return std::ranges::all_of(std::views::zip(std::views::iota(1UZ), tail.substr(1)),
                               [&](const auto& indexed) -> bool {
                                   const auto [i, c] = indexed;
                                   const auto b = static_cast<uint8_t>(c);
                                   return i == 1 ? (b >= lo2 && b <= hi2) : (b >= 0x80 && b <= 0xBF);
                               });
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
    const auto digest = hash::sha1(joined);
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
inline auto encode_frame(Opcode op, std::string_view payload, bool fin = true, bool rsv1 = false) -> std::string {
    std::string out;
    out.reserve(payload.size() + 10);
    out += static_cast<char>((fin ? 0x80U : 0x00U) | (rsv1 ? 0x40U : 0x00U) | static_cast<uint8_t>(op));
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
            out += static_cast<char>((static_cast<uint64_t>(n) >> (static_cast<unsigned>(i) * 8U)) & 0xFFU);
        }
    }
    out += payload;
    return out;
}

// permessage-deflate (RFC 7692): what the server agreed to, and the response line that says so.
struct Deflate {
    bool enabled = false;
    bool server_no_ctx = false; // the client asked us to reset our compressor after each message
    int server_bits = 15;       // our compression window
    std::string response;       // value for Sec-WebSocket-Extensions
};

namespace detail {

// `s` without leading and trailing characters from `chars`.
inline auto trim(std::string_view s, std::string_view chars = " ") -> std::string_view {
    const auto first = s.find_first_not_of(chars);
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(chars) - first + 1);
}

inline auto lower(std::string_view s) -> std::string {
    return s | std::views::transform([](char c) -> char {
               return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
           }) |
           std::ranges::to<std::string>();
}

// A window size in 8..15, optionally quoted. nullopt for anything else.
inline auto window_bits(std::string_view v) -> std::optional<int> {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        v = v.substr(1, v.size() - 2);
    }
    if (v.empty() || v.size() > 2 || !std::ranges::all_of(v, [](char c) -> bool { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    const int n = std::stoi(std::string(v));
    if (n < 8 || n > 15) {
        return std::nullopt;
    }
    return n;
}

} // namespace detail

namespace detail {
// One offer's agreement, or nullopt when it isn't permessage-deflate or has a parameter we can't honor.
inline auto accept_deflate_offer(std::string_view offer) -> std::optional<Deflate> {
    const auto semi = offer.find(';');
    if (lower(trim(offer.substr(0, semi))) != "permessage-deflate") {
        return std::nullopt;
    }
    std::string_view rest = semi == std::string_view::npos ? std::string_view{} : offer.substr(semi + 1);
    if (rest.ends_with(';')) {
        rest.remove_suffix(1); // a trailing ';' ends the list rather than adding an empty parameter
    }

    Deflate d;
    d.enabled = true;
    std::string response = "permessage-deflate";
    bool seen_sn = false;
    bool seen_cn = false;
    bool seen_sb = false;
    bool seen_cb = false;
    for (auto part : rest | std::views::split(';')) {
        const auto param = trim(std::string_view(part));
        const auto eq = param.find('=');
        const auto key = lower(trim(param.substr(0, eq)));
        const bool has_value = eq != std::string_view::npos;
        const auto value = has_value ? trim(param.substr(eq + 1)) : std::string_view{};

        if (key == "server_no_context_takeover" && !has_value && !seen_sn) {
            seen_sn = true;
            d.server_no_ctx = true;
            response += "; server_no_context_takeover";
        } else if (key == "client_no_context_takeover" && !has_value && !seen_cn) {
            // Accepted but not echoed: the client may keep its context, so our inflater always keeps its.
            seen_cn = true;
        } else if (key == "server_max_window_bits" && !seen_sb) {
            seen_sb = true;
            if (has_value) {
                // zlib cannot make a raw 8-bit window, so 8 is declined rather than rounded up.
                const auto bits = window_bits(value);
                if (!bits || *bits == 8) {
                    return std::nullopt;
                } else {
                    d.server_bits = *bits;
                    response += std::format("; server_max_window_bits={}", *bits);
                }
            }
        } else if (key == "client_max_window_bits" && !seen_cb) {
            seen_cb = true;
            if (has_value && !window_bits(value)) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    d.response = std::move(response);
    return d;
}
} // namespace detail

// Picks the first permessage-deflate offer we can accept; anything we can't honor is declined.
inline auto negotiate_deflate(std::string_view header) -> Deflate {
    for (auto offer : header | std::views::split(',')) {
        if (auto d = detail::accept_deflate_offer(detail::trim(std::string_view(offer)))) {
            return *d;
        }
    }
    return {};
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

    [[nodiscard]] static auto in_use() -> int {
        return counter().load();
    }

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
    [[nodiscard]] static auto make(int fd, Slot slot,
                                   int idle_timeout_ms = k_idle_timeout_ms) -> std::shared_ptr<Conn> {
        if (idle_timeout_ms > 0) {
            timeval tv{.tv_sec = idle_timeout_ms / 1000, .tv_usec = (idle_timeout_ms % 1000) * 1000};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        timeval send_tv{.tv_sec = k_send_timeout_ms / 1000, .tv_usec = (k_send_timeout_ms % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv));
        return std::shared_ptr<Conn>(new Conn(fd, std::move(slot)));
    }
    ~Conn() {
        if (deflate_) {
            deflateEnd(&zout_);
            inflateEnd(&zin_);
        }
        ::close(fd_);
    }
    Conn(const Conn&) = delete;
    auto operator=(const Conn&) -> Conn& = delete;
    Conn(Conn&&) = delete;
    auto operator=(Conn&&) -> Conn& = delete;

    [[nodiscard]] auto open() const -> bool {
        return open_.load();
    }

    // Switches on permessage-deflate for this connection; call once, right after the 101 is sent.
    void set_deflate(const Deflate& d) {
        if (!d.enabled) {
            return;
        }
        if (deflateInit2(&zout_, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -d.server_bits, 8, Z_DEFAULT_STRATEGY) != Z_OK ||
            inflateInit2(&zin_, -15) != Z_OK) {
            throw Error("zlib initialization failed");
        }
        deflate_ = true;
        server_no_ctx_ = d.server_no_ctx;
    }

    // Bytes that arrived after the upgrade request's headers; consumed before the socket is read.
    void prime(std::string bytes) {
        inbuf_ = std::move(bytes);
    }

    // Next data message, or nullopt once the peer closes or stops answering pings.
    // Throws Error after sending a close frame on protocol violations.
    auto recv() -> std::optional<Message> {
        Message msg;
        bool in_fragment = false;
        bool compressed = false; // the current message arrived with RSV1 set
        bool ping_outstanding = false;
        std::string utf8_pending; // trailing bytes of a text message split mid-codepoint
        detail::Budget held;      // what this message holds in the process-wide budget

        // Appends decoded message bytes; text is checked for UTF-8 as it arrives.
        auto absorb = [&](std::string_view bytes) -> void {
            if (msg.data.size() + bytes.size() > k_max_message) {
                fail(CloseCode::TooBig, "message too large");
            }
            if (!msg.binary) {
                const std::string chunk = utf8_pending + std::string(bytes);
                const size_t cut = detail::utf8_cut(chunk);
                const std::string_view view(chunk);
                if (!detail::utf8_valid(view.substr(0, cut)) || !detail::utf8_tail_ok(view.substr(cut))) {
                    fail(CloseCode::InvalidData, "invalid UTF-8 in text message");
                }
                utf8_pending = chunk.substr(cut);
            }
            if (!held.reserve(bytes.size())) {
                fail(CloseCode::TryAgainLater, "too many bytes buffered");
            }
            msg.data += bytes;
        };

        // Runs compressed bytes through the inflater. Z_SYNC_FLUSH only at the end of a message.
        auto inflate_into = [&](std::string_view input, int flush) -> void {
            zin_.next_in = c_api::zlib_input(input);
            zin_.avail_in = static_cast<uInt>(input.size());
            std::array<unsigned char, 16384> out{};
            do {
                zin_.next_out = out.data();
                zin_.avail_out = static_cast<uInt>(out.size());
                const int rc = inflate(&zin_, flush);
                if (rc == Z_NEED_DICT || rc == Z_DATA_ERROR || rc == Z_MEM_ERROR || rc == Z_STREAM_ERROR) {
                    fail(CloseCode::InvalidData, "invalid permessage-deflate data");
                }
                absorb(c_api::chars(std::span<const unsigned char>(out)).substr(0, out.size() - zin_.avail_out));
            } while (zin_.avail_out == 0);
        };

        while (open_.load()) {
            IoStatus st = IoStatus::Ok;
            const auto head = read_header(st);
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
            if (!head) {
                open_ = false;
                return std::nullopt;
            }
            ping_outstanding = false;

            const auto op = head->op;
            if (op == Opcode::Ping) {
                const auto payload = read_control(*head, st);
                if (!payload) {
                    open_ = false;
                    return std::nullopt;
                }
                write(Opcode::Pong, *payload);
                continue;
            }
            if (op == Opcode::Pong) {
                if (!read_control(*head, st)) {
                    open_ = false;
                    return std::nullopt;
                }
                continue;
            }
            if (op == Opcode::Close) {
                const auto payload = read_control(*head, st);
                if (!payload) {
                    open_ = false;
                    return std::nullopt;
                }
                validate_close_payload(*payload);
                write(Opcode::Close, *payload);
                open_ = false;
                ::shutdown(fd_, SHUT_RDWR);
                return std::nullopt;
            }
            if (op == Opcode::Continuation) {
                if (!in_fragment) {
                    fail(CloseCode::Protocol, "unexpected continuation frame");
                }
                if (head->rsv1) {
                    fail(CloseCode::Protocol, "RSV1 on a continuation frame");
                }
            } else {
                if (in_fragment) {
                    fail(CloseCode::Protocol, "data frame inside fragmented message");
                }
                msg.binary = op == Opcode::Binary;
                compressed = head->rsv1;
                in_fragment = !head->fin;
            }
            // Compressed sizes don't bound the message; the decoded size is checked in absorb().
            if (!compressed && msg.data.size() + head->len > k_max_message) {
                fail(CloseCode::TooBig, "message too large");
            }

            const bool ok =
                compressed
                    ? read_payload(*head, st, [&](std::string_view bytes) -> void { inflate_into(bytes, Z_NO_FLUSH); })
                    : read_payload(*head, st, [&](std::string_view bytes) -> void { absorb(bytes); });
            if (!ok) {
                open_ = false;
                return std::nullopt;
            }
            if (head->fin) {
                if (compressed) {
                    // RFC 7692 §7.2.2: the removed 00 00 FF FF is restored before the final flush.
                    inflate_into(std::string_view("\0\0\xff\xff", 4), Z_SYNC_FLUSH);
                }
                if (!utf8_pending.empty()) {
                    fail(CloseCode::InvalidData, "truncated UTF-8 in text message");
                }
                return msg;
            }
        }
        return std::nullopt;
    }

    // Returns false if the connection is closed or the write failed.
    auto send_text(std::string_view text) -> bool {
        return write(Opcode::Text, text);
    }
    auto send_binary(std::string_view bytes) -> bool {
        return write(Opcode::Binary, bytes);
    }

    void close(CloseCode code = CloseCode::Normal) {
        const auto raw = static_cast<uint16_t>(code);
        const std::string payload{static_cast<char>(raw >> 8), static_cast<char>(raw & 0xFF)};
        const std::scoped_lock lock(write_mu_);
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

    struct Header {
        bool fin = false;
        bool rsv1 = false;
        Opcode op = Opcode::Continuation;
        uint64_t len = 0;
        std::array<char, 4> mask{};
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
        const auto code = static_cast<uint16_t>((static_cast<unsigned>(static_cast<uint8_t>(payload.at(0))) << 8U) |
                                                static_cast<unsigned>(static_cast<uint8_t>(payload.at(1))));
        if (!detail::close_code_valid(code)) {
            fail(CloseCode::Protocol, "invalid close code");
        }
        if (payload.size() > 2 && !detail::utf8_valid(std::string_view(payload).substr(2))) {
            fail(CloseCode::InvalidData, "invalid UTF-8 in close reason");
        }
    }

    // Reads 1 to n bytes, or returns 0 with st set: Timeout if the socket timed out before any byte, else Closed.
    auto read_some(char* buf, size_t n, IoStatus& st) -> size_t {
        if (!inbuf_.empty()) {
            const size_t take = std::min(n, inbuf_.size());
            std::memcpy(buf, inbuf_.data(), take);
            inbuf_.erase(0, take);
            return take;
        }
        while (true) {
            const ssize_t r = ::recv(fd_, buf, n, 0);
            if (r > 0) {
                const auto now = std::chrono::steady_clock::now();
                if (frame_deadline_ == std::chrono::steady_clock::time_point::max()) {
                    frame_deadline_ = now + k_frame_deadline; // armed by the frame's first byte, not by idle time
                }
                if (now > frame_deadline_) {
                    st = IoStatus::Closed;
                    return 0;
                }
                return static_cast<size_t>(r);
            }
            if (r < 0 && errno == EINTR) {
                continue;
            }
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && st == IoStatus::Ok) {
                st = IoStatus::Timeout;
                return 0;
            }
            st = IoStatus::Closed;
            return 0;
        }
    }

    // Reads exactly n bytes. A timeout before any byte arrives reports Timeout; one mid-read is a close.
    auto read_exact(char* buf, size_t n, IoStatus& st) -> bool {
        size_t got = 0;
        while (got < n) {
            const size_t r = read_some(std::span(buf, n).subspan(got).data(), n - got, st);
            if (r == 0) {
                if (got > 0 && st == IoStatus::Timeout) {
                    st = IoStatus::Closed;
                }
                return false;
            }
            got += r;
        }
        return true;
    }

    auto read_header(IoStatus& st) -> std::optional<Header> {
        frame_deadline_ = std::chrono::steady_clock::time_point::max();
        std::array<char, 2> head{};
        if (!read_exact(head.data(), head.size(), st)) {
            return std::nullopt;
        }
        const auto b0 = static_cast<uint8_t>(head.at(0));
        const auto b1 = static_cast<uint8_t>(head.at(1));
        if ((b0 & 0x30) != 0) {
            fail(CloseCode::Protocol, "reserved bits set");
        }
        const bool rsv1 = (b0 & 0x40) != 0;
        if (rsv1 && !deflate_) {
            fail(CloseCode::Protocol, "RSV1 without permessage-deflate");
        }
        if ((b1 & 0x80) == 0) {
            fail(CloseCode::Protocol, "client frame not masked");
        }

        const uint8_t raw_op = b0 & 0x0F;
        if ((raw_op >= 0x3 && raw_op <= 0x7) || raw_op >= 0xB) {
            fail(CloseCode::Protocol, "reserved opcode");
        }
        Header h;
        h.fin = (b0 & 0x80) != 0;
        h.op = static_cast<Opcode>(raw_op);
        const bool control = (raw_op & 0x08) != 0;

        // RFC 6455 §5.2: the smallest length encoding is required, and the 64-bit high bit must be 0.
        const uint8_t len_marker = b1 & 0x7F;
        uint64_t len = len_marker;
        if (len_marker == 126) {
            std::array<char, 2> ext{};
            if (!read_exact(ext.data(), ext.size(), st)) {
                return fail_mid_frame<Header>(st);
            }
            len = (static_cast<uint64_t>(static_cast<uint8_t>(ext.at(0))) << 8) | static_cast<uint8_t>(ext.at(1));
            if (len < 126) {
                fail(CloseCode::Protocol, "non-minimal payload length");
            }
        } else if (len_marker == 127) {
            std::array<char, 8> ext{};
            if (!read_exact(ext.data(), ext.size(), st)) {
                return fail_mid_frame<Header>(st);
            }
            if ((static_cast<uint8_t>(ext.at(0)) & 0x80) != 0) {
                fail(CloseCode::Protocol, "payload length high bit set");
            }
            len = std::ranges::fold_left(ext, uint64_t{0}, [](uint64_t acc, char c) -> uint64_t {
                return (acc << 8) | static_cast<uint8_t>(c);
            });
            if (len <= 0xFFFF) {
                fail(CloseCode::Protocol, "non-minimal payload length");
            }
        }
        if (control && (!h.fin || len > 125 || rsv1)) {
            fail(CloseCode::Protocol, "invalid control frame");
        }
        if (len > k_max_message) {
            fail(CloseCode::TooBig, "frame too large");
        }
        h.len = len;
        h.rsv1 = rsv1;

        if (!read_exact(h.mask.data(), h.mask.size(), st)) {
            return fail_mid_frame<Header>(st);
        }
        return h;
    }

    // Reads and unmasks a payload in pieces; on_bytes sees each piece before the rest of the frame arrives.
    template<typename OnBytes>
    auto read_payload(const Header& h, IoStatus& st, OnBytes&& on_bytes) -> bool {
        std::array<char, 16384> buf{};
        uint64_t done = 0;
        while (done < h.len) {
            const auto want = static_cast<size_t>(std::min<uint64_t>(buf.size(), h.len - done));
            const size_t got = read_some(buf.data(), want, st);
            if (got == 0) {
                if (st == IoStatus::Timeout) {
                    st = IoStatus::Closed; // a dead peer mid-frame, not an idle one
                }
                return false;
            }
            for (size_t i = 0; i < got; ++i) {
                buf.at(i) = static_cast<char>(static_cast<unsigned>(static_cast<uint8_t>(buf.at(i))) ^
                                              static_cast<unsigned>(static_cast<uint8_t>(h.mask.at((done + i) % 4))));
            }
            on_bytes(std::string_view(buf.data(), got));
            done += got;
        }
        return true;
    }

    // Control frames are at most 125 bytes, so they can be held whole.
    auto read_control(const Header& h, IoStatus& st) -> std::optional<std::string> {
        std::string out;
        if (!read_payload(h, st, [&](std::string_view bytes) -> void { out.append(bytes); })) {
            return std::nullopt;
        }
        return out;
    }

    // A timeout after the frame has started is a dead peer, not an idle one.
    template<typename T>
    static auto fail_mid_frame(IoStatus& st) -> std::optional<T> {
        if (st == IoStatus::Timeout) {
            st = IoStatus::Closed;
        }
        return std::nullopt;
    }

    auto write(Opcode op, std::string_view payload) -> bool {
        const std::scoped_lock lock(write_mu_);
        if (!open_.load()) {
            return false;
        }
        if (deflate_ && (op == Opcode::Text || op == Opcode::Binary)) {
            return send_all(encode_frame(op, compress(payload), true, true));
        }
        return send_all(encode_frame(op, payload));
    }

    // Caller holds write_mu_. RFC 7692 §7.2.1: a sync flush, with its 00 00 FF FF tail removed.
    auto compress(std::string_view in) -> std::string {
        if (server_no_ctx_) {
            deflateReset(&zout_);
        }
        zout_.next_in = c_api::zlib_input(in);
        zout_.avail_in = static_cast<uInt>(in.size());
        std::string out;
        std::array<unsigned char, 16384> buf{};
        do {
            zout_.next_out = buf.data();
            zout_.avail_out = static_cast<uInt>(buf.size());
            deflate(&zout_, Z_SYNC_FLUSH);
            out.append(c_api::chars(std::span<const unsigned char>(buf)).substr(0, buf.size() - zout_.avail_out));
        } while (zout_.avail_out == 0);
        constexpr std::string_view kTail("\0\0\xff\xff", 4);
        if (out.size() >= kTail.size() && out.ends_with(kTail)) {
            out.resize(out.size() - kTail.size());
        }
        return out;
    }

    // Caller holds write_mu_.
    auto send_all(const std::string& bytes) -> bool {
        size_t sent = 0;
        while (sent < bytes.size()) {
            const ssize_t r = ::send(fd_, std::string_view(bytes).substr(sent).data(), bytes.size() - sent, 0);
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
    bool deflate_ = false;
    bool server_no_ctx_ = false; // we reset our compressor after each message
    z_stream zin_{};
    z_stream zout_{};
    std::string inbuf_; // bytes read with the upgrade request, served before the socket
    std::chrono::steady_clock::time_point frame_deadline_{};
    std::atomic<bool> open_{true};
    std::mutex write_mu_;
};

} // namespace web::ws
