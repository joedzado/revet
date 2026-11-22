#pragma once

#include "web.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <functional>
#include <cstdlib>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>

// Token-bucket rate limiting for route guards and global middleware.
//
//   static const auto login_limit = web::rate_limit::Limiter::make({.burst = 10, .per_second = 0.2});
//   route_guards() -> { web::rate_limit::by_ip(login_limit) }
//
// Buckets live in memory, so limits reset on restart and are per process.
namespace web::rate_limit {

using Clock = std::chrono::steady_clock;

struct Rule {
    double burst = 10;      // bucket size: requests allowed at once
    double per_second = 1;  // refill rate
    std::size_t max_keys = 100'000; // memory bound; least recently used keys are dropped beyond this
};

struct Decision {
    bool allowed = true;
    std::chrono::seconds retry_after{0}; // when !allowed: seconds until one request fits
};

class Limiter {
public:
    explicit Limiter(Rule rule) : rule_(sanitize(rule)) {}

    [[nodiscard]] static auto make(Rule rule) -> std::shared_ptr<Limiter> { return std::make_shared<Limiter>(rule); }

    // Spend one token from `key`'s bucket. `now` is injectable for tests.
    auto take(std::string_view key, Clock::time_point now = Clock::now()) -> Decision {
        const std::scoped_lock lock(mu_);
        expire(now);

        auto it = index_.find(key);
        if (it == index_.end()) {
            lru_.push_front({std::string(key), rule_.burst, now});
            it = index_.emplace(lru_.front().key, lru_.begin()).first;
            while (index_.size() > rule_.max_keys) {
                index_.erase(lru_.back().key);
                lru_.pop_back();
            }
        } else {
            lru_.splice(lru_.begin(), lru_, it->second);
        }

        auto& b = lru_.front();
        const double elapsed = std::chrono::duration<double>(now - b.updated).count();
        if (elapsed > 0) {
            b.tokens = std::min(rule_.burst, b.tokens + (elapsed * rule_.per_second));
            b.updated = now;
        }
        if (b.tokens >= 1.0) {
            b.tokens -= 1.0;
            return {};
        }
        const double wait = (1.0 - b.tokens) / rule_.per_second;
        return {.allowed = false, .retry_after = std::chrono::seconds(std::max<long>(1, std::lround(std::ceil(wait))))};
    }

    [[nodiscard]] auto size() const -> std::size_t {
        const std::scoped_lock lock(mu_);
        return index_.size();
    }

    [[nodiscard]] auto rule() const -> const Rule& { return rule_; }

private:
    struct Bucket {
        std::string key;
        double tokens;
        Clock::time_point updated;
    };
    struct SvHash {
        using is_transparent = void;
        auto operator()(std::string_view s) const -> std::size_t { return std::hash<std::string_view>{}(s); }
    };

    static auto sanitize(Rule r) -> Rule {
        if (!(r.burst >= 1)) r.burst = 1;
        if (!(r.per_second > 0)) r.per_second = 1e-6;
        if (r.max_keys == 0) r.max_keys = 1;
        return r;
    }

    // A bucket idle long enough to be full again is the same as no bucket: drop it.
    void expire(Clock::time_point now) {
        const auto full_after = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(rule_.burst / rule_.per_second));
        while (!lru_.empty() && now - lru_.back().updated >= full_after) {
            index_.erase(lru_.back().key);
            lru_.pop_back();
        }
    }

    Rule rule_;
    mutable std::mutex mu_;
    std::list<Bucket> lru_; // most recently used first
    std::unordered_map<std::string_view, std::list<Bucket>::iterator, SvHash, std::equal_to<>> index_;
};

// ── Client address ───────────────────────────────────────────────────────────

namespace detail {

struct Net {
    std::array<unsigned char, 16> addr{};
    int family = 0; // AF_INET or AF_INET6
    int prefix = 0;
};

inline auto parse_ip(std::string_view text, std::array<unsigned char, 16>& out) -> int {
    const std::string s(text);
    if (inet_pton(AF_INET, s.c_str(), out.data()) == 1) return AF_INET;
    if (inet_pton(AF_INET6, s.c_str(), out.data()) == 1) return AF_INET6;
    return 0;
}

inline auto trim(std::string_view s) -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// "10.0.0.1", "10.0.0.0/8", "::1", "fd00::/8". Unparseable entries are ignored.
inline auto parse_net(std::string_view text) -> std::optional<Net> {
    text = trim(text);
    Net n;
    std::string_view addr = text;
    int prefix = -1;
    if (const auto slash = text.find('/'); slash != std::string_view::npos) {
        addr = text.substr(0, slash);
        const auto p = text.substr(slash + 1);
        int v = 0;
        const auto [end, ec] = std::from_chars(p.data(), p.data() + p.size(), v);
        if (ec != std::errc{} || end != p.data() + p.size() || v < 0) return std::nullopt;
        prefix = v;
    }
    n.family = parse_ip(addr, n.addr);
    if (n.family == 0) return std::nullopt;
    const int max_bits = n.family == AF_INET ? 32 : 128;
    if (prefix < 0) prefix = max_bits;
    if (prefix > max_bits) return std::nullopt;
    n.prefix = prefix;
    return n;
}

inline auto in_net(const Net& n, std::string_view ip) -> bool {
    std::array<unsigned char, 16> a{};
    if (parse_ip(ip, a) != n.family) return false;
    int bits = n.prefix;
    for (std::size_t i = 0; bits > 0; ++i, bits -= 8) {
        const unsigned mask = bits >= 8 ? 0xFFU : (0xFFU << (8 - bits)) & 0xFFU;
        if ((a.at(i) & mask) != (n.addr.at(i) & mask)) return false;
    }
    return true;
}

} // namespace detail

// Proxies whose forwarding header is believed. Empty means nobody is trusted and the
// socket address is always the client.
class TrustedProxies {
public:
    TrustedProxies() = default;
    explicit TrustedProxies(std::string_view comma_separated) {
        while (!comma_separated.empty()) {
            const auto comma = comma_separated.find(',');
            if (auto net = detail::parse_net(comma_separated.substr(0, comma))) nets_.push_back(*net);
            if (comma == std::string_view::npos) break;
            comma_separated.remove_prefix(comma + 1);
        }
    }

    [[nodiscard]] auto contains(std::string_view ip) const -> bool {
        return std::ranges::any_of(nets_, [&](const detail::Net& n) { return detail::in_net(n, ip); });
    }
    [[nodiscard]] auto empty() const -> bool { return nets_.empty(); }

private:
    std::vector<detail::Net> nets_;
};

// Process-wide default, read once from REVET_TRUSTED_PROXIES (comma separated addresses or CIDRs).
inline auto trusted_proxies() -> TrustedProxies& {
    static TrustedProxies tp = [] {
        const char* env = std::getenv("REVET_TRUSTED_PROXIES");
        return env != nullptr ? TrustedProxies(env) : TrustedProxies();
    }();
    return tp;
}

// The address of the client. The forwarding header (X-Forwarded-For) is used only when the socket
// peer is a trusted proxy; then the list is walked from the right and the first address that is not
// itself a trusted proxy is the client. A client-supplied header from an untrusted peer is ignored.
inline auto client_ip(const Request& req, const TrustedProxies& trusted) -> std::string {
    if (req.peer_ip.empty() || !trusted.contains(req.peer_ip)) {
        return req.peer_ip;
    }
    const auto header = req.headers.get("x-forwarded-for");
    std::string_view rest = header;
    std::string result = req.peer_ip;
    while (!rest.empty()) {
        const auto comma = rest.rfind(',');
        const auto item = detail::trim(comma == std::string_view::npos ? rest : rest.substr(comma + 1));
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(0, comma);
        std::array<unsigned char, 16> tmp{};
        if (detail::parse_ip(item, tmp) == 0) {
            break; // garbage in the chain: stop at the last address we could vouch for
        }
        result = std::string(item);
        if (!trusted.contains(item)) {
            break;
        }
    }
    return result;
}

inline auto client_ip(const Request& req) -> std::string {
    return client_ip(req, trusted_proxies());
}

// ── Middleware ───────────────────────────────────────────────────────────────

inline auto too_many(const Request& req, std::chrono::seconds retry_after) -> Response {
    Response resp;
    if (req.path.path.starts_with("/api/") || req.headers.get("accept").contains("application/json")) {
        resp = api::error(status::too_many_requests, "rate limit exceeded");
    } else {
        resp.body = "429 Too Many Requests";
        resp.content_type = "text/plain";
        resp.status = status::too_many_requests;
    }
    resp.headers.data["Retry-After"] = std::to_string(retry_after.count());
    return resp;
}

// Limit by an arbitrary key derived from the request (account name, user id, route + IP).
// An empty key skips the limit for that request.
inline auto by_key(std::shared_ptr<Limiter> limiter, std::function<std::string(const Request&)> key_of) -> Middleware {
    return [limiter = std::move(limiter), key_of = std::move(key_of)](const Request& req, const Next& next) -> Response {
        const auto key = key_of(req);
        if (key.empty()) {
            return next();
        }
        const auto d = limiter->take(key);
        return d.allowed ? next() : too_many(req, d.retry_after);
    };
}

// Limit by client address. Requests with no known address share one bucket, so a failure to
// identify the peer limits harder rather than not at all.
inline auto by_ip(std::shared_ptr<Limiter> limiter, const TrustedProxies* trusted = nullptr) -> Middleware {
    return by_key(std::move(limiter), [trusted](const Request& req) {
        auto ip = client_ip(req, trusted != nullptr ? *trusted : trusted_proxies());
        return ip.empty() ? std::string("unknown") : ip;
    });
}

} // namespace web::rate_limit
