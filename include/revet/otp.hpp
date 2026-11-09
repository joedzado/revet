#pragma once
// Authenticator-app codes (TOTP, RFC 6238: SHA-1, 6 digits, 30 s) and a login throttle.
//
//   auto secret = web::otp::base32_decode(stored_base32);
//   static web::otp::ReplayGuard guard;                // one per account
//   if (!guard.verify(secret, code)) ...
//
//   static web::otp::Throttle throttle{5, std::chrono::minutes{15}};
//   if (throttle.locked(username)) return locked_page();
//   ok ? throttle.reset(username) : throttle.fail(username);

#include "crypto.hpp"
#include "hash.hpp"

#include <chrono>
#include <cstdint>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace web::otp {

inline auto base32_decode(std::string_view text) -> std::string {
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char raw : text) {
        const char c = (raw >= 'a' && raw <= 'z') ? static_cast<char>(raw - 'a' + 'A') : raw;
        std::uint32_t value = 0;
        if (c >= 'A' && c <= 'Z') {
            value = static_cast<std::uint32_t>(c - 'A');
        } else if (c >= '2' && c <= '7') {
            value = static_cast<std::uint32_t>(c - '2') + 26;
        } else {
            continue; // padding and spaces
        }
        buffer = (buffer << 5U) | value;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> static_cast<unsigned>(bits)) & 0xFFU);
        }
    }
    return out;
}

// RFC 4648 base32 without padding, as authenticator apps expect in otpauth:// URLs.
inline auto base32_encode(std::string_view bytes) -> std::string {
    static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char ch : bytes) {
        buffer = (buffer << 8U) | static_cast<std::uint8_t>(ch);
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out += alphabet[(buffer >> static_cast<unsigned>(bits)) & 0x1FU];
        }
    }
    if (bits > 0) {
        out += alphabet[(buffer << static_cast<unsigned>(5 - bits)) & 0x1FU];
    }
    return out;
}

inline auto now_step(std::chrono::system_clock::time_point t = std::chrono::system_clock::now()) -> std::uint64_t {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count()) /
           30;
}

// 6 digits for the 30-second step `counter`. `secret` is raw bytes (base32_decode it first).
inline auto code(std::string_view secret, std::uint64_t counter) -> std::string {
    std::string message(8, '\0');
    for (int i = 7; i >= 0; --i) {
        message[static_cast<std::size_t>(i)] = static_cast<char>(counter & 0xFFU);
        counter >>= 8U;
    }
    const auto mac = hash::hmac_sha1(std::string(secret), message);
    const std::size_t offset = mac[19] & 0x0FU;
    const std::uint32_t value = ((static_cast<std::uint32_t>(mac[offset]) & 0x7FU) << 24U) |
                                (static_cast<std::uint32_t>(mac[offset + 1]) << 16U) |
                                (static_cast<std::uint32_t>(mac[offset + 2]) << 8U) |
                                static_cast<std::uint32_t>(mac[offset + 3]);
    return std::format("{:06}", value % 1000000);
}

// The step `code` matches, allowing one step of clock drift either way; 0 if none.
inline auto matching_step(std::string_view secret, std::string_view submitted, std::uint64_t now = now_step())
    -> std::uint64_t {
    for (const std::uint64_t step : {now - 1, now, now + 1}) {
        if (crypto::secure_eq(code(secret, step), submitted)) {
            return step;
        }
    }
    return 0;
}

// Accepts each code once: a code seen at or before the last accepted step is refused.
class ReplayGuard {
public:
    auto verify(std::string_view secret, std::string_view submitted, std::uint64_t now = now_step()) -> bool {
        const std::lock_guard lock(mu_);
        const auto step = matching_step(secret, submitted, now);
        if (step == 0 || step <= last_) {
            return false;
        }
        last_ = step;
        return true;
    }

private:
    std::mutex mu_;
    std::uint64_t last_ = 0;
};

// After `max_failures` failures for a key, that key is locked for `lockout`.
class Throttle {
public:
    Throttle(int max_failures, std::chrono::seconds lockout) : max_(max_failures), lockout_(lockout) {}

    auto locked(const std::string& key) -> bool {
        const std::lock_guard lock(mu_);
        auto it = entries_.find(key);
        return it != entries_.end() && it->second.until > std::chrono::steady_clock::now();
    }
    void fail(const std::string& key) {
        const std::lock_guard lock(mu_);
        // Keys can be attacker-chosen (usernames), so don't let the map grow without bound.
        if (entries_.size() >= 10000) {
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(entries_, [&](const auto& kv) { return kv.second.until <= now; });
        }
        auto& e = entries_[key];
        if (++e.failures >= max_) {
            e.until = std::chrono::steady_clock::now() + lockout_;
            e.failures = 0;
        }
    }
    void reset(const std::string& key) {
        const std::lock_guard lock(mu_);
        entries_.erase(key);
    }

private:
    struct Entry {
        int failures = 0;
        std::chrono::steady_clock::time_point until{};
    };
    std::mutex mu_;
    int max_;
    std::chrono::seconds lockout_;
    std::map<std::string, Entry> entries_;
};

} // namespace web::otp
