// Tests for web::rate_limit — token buckets, client address resolution behind a
// proxy, and the 429 middleware.
#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <netinet/in.h>
#include <revet/rate_limit.hpp>
#include <sys/socket.h>
#include <unistd.h>

using namespace std::chrono_literals;
namespace rl = web::rate_limit;

namespace {
const rl::Clock::time_point t0 = rl::Clock::time_point{} + 1000s;

auto request_from(std::string peer, std::string xff = {}) -> web::Request {
    web::Request req;
    req.peer_ip = std::move(peer);
    if (!xff.empty()) {
        req.headers.data["x-forwarded-for"] = std::move(xff);
    }
    return req;
}

auto run(const web::Middleware& mw, const web::Request& req, bool* reached = nullptr) -> web::Response {
    return web::dispatch(req,
                         [&](const web::Request&) {
                             if (reached != nullptr) {
                                 *reached = true;
                             }
                             return web::Response{.body = "ok"};
                         },
                         {mw});
}
} // namespace

TEST_CASE("limiter: a burst is allowed, then requests are refused", "[rate_limit]") {
    rl::Limiter lim({.burst = 3, .per_second = 1});
    REQUIRE(lim.take("a", t0).allowed);
    REQUIRE(lim.take("a", t0).allowed);
    REQUIRE(lim.take("a", t0).allowed);
    const auto d = lim.take("a", t0);
    REQUIRE_FALSE(d.allowed);
    REQUIRE(d.retry_after == 1s);
}

TEST_CASE("limiter: tokens refill over time and never exceed the burst", "[rate_limit]") {
    rl::Limiter lim({.burst = 2, .per_second = 0.5}); // one token every 2 s
    REQUIRE(lim.take("a", t0).allowed);
    REQUIRE(lim.take("a", t0).allowed);
    const auto refused = lim.take("a", t0);
    REQUIRE_FALSE(refused.allowed);
    REQUIRE(refused.retry_after == 2s);

    REQUIRE_FALSE(lim.take("a", t0 + 1s).allowed);
    REQUIRE(lim.take("a", t0 + 2s).allowed);
    REQUIRE_FALSE(lim.take("a", t0 + 2s).allowed);

    // A long idle period refills to the burst, not beyond it.
    REQUIRE(lim.take("a", t0 + 1h).allowed);
    REQUIRE(lim.take("a", t0 + 1h).allowed);
    REQUIRE_FALSE(lim.take("a", t0 + 1h).allowed);
}

TEST_CASE("limiter: keys do not share buckets", "[rate_limit]") {
    rl::Limiter lim({.burst = 1, .per_second = 0.1});
    REQUIRE(lim.take("alice", t0).allowed);
    REQUIRE_FALSE(lim.take("alice", t0).allowed);
    REQUIRE(lim.take("bob", t0).allowed);
}

TEST_CASE("limiter: idle buckets expire", "[rate_limit]") {
    rl::Limiter lim({.burst = 2, .per_second = 1}); // full again after 2 s
    lim.take("a", t0);
    lim.take("b", t0);
    REQUIRE(lim.size() == 2);
    lim.take("c", t0 + 10s);
    REQUIRE(lim.size() == 1);
}

TEST_CASE("limiter: memory is bounded and least recently used keys go first", "[rate_limit]") {
    rl::Limiter lim({.burst = 1, .per_second = 0.001, .max_keys = 3});
    lim.take("a", t0);
    lim.take("b", t0 + 1s);
    lim.take("c", t0 + 2s);
    lim.take("a", t0 + 3s); // touch a, so b is now the oldest
    lim.take("d", t0 + 4s);
    REQUIRE(lim.size() == 3);
    REQUIRE_FALSE(lim.take("a", t0 + 5s).allowed); // a kept its spent bucket
    REQUIRE(lim.take("b", t0 + 5s).allowed);       // b was dropped, so it starts fresh

    for (int i = 0; i < 1000; ++i) {
        lim.take("flood" + std::to_string(i), t0 + 6s);
    }
    REQUIRE(lim.size() <= 3);
}

TEST_CASE("limiter: nonsense rules are clamped instead of misbehaving", "[rate_limit]") {
    rl::Limiter lim({.burst = 0, .per_second = 0, .max_keys = 0});
    REQUIRE(lim.take("a", t0).allowed);
    REQUIRE_FALSE(lim.take("a", t0).allowed);
}

TEST_CASE("client_ip: no trusted proxy means the socket address is the client", "[rate_limit]") {
    const rl::TrustedProxies none;
    REQUIRE(rl::client_ip(request_from("203.0.113.9", "1.2.3.4"), none) == "203.0.113.9");
}

TEST_CASE("client_ip: a spoofed forwarding header from an untrusted peer is ignored", "[rate_limit]") {
    const rl::TrustedProxies trusted("10.0.0.1");
    REQUIRE(rl::client_ip(request_from("198.51.100.7", "1.2.3.4"), trusted) == "198.51.100.7");
}

TEST_CASE("client_ip: a trusted proxy's header names the client", "[rate_limit]") {
    const rl::TrustedProxies trusted("10.0.0.1");
    REQUIRE(rl::client_ip(request_from("10.0.0.1", "203.0.113.9"), trusted) == "203.0.113.9");
}

TEST_CASE("client_ip: a client cannot prepend a fake address to the chain", "[rate_limit]") {
    const rl::TrustedProxies trusted("10.0.0.1");
    // The client sent "6.6.6.6"; the proxy appended the real peer it saw.
    REQUIRE(rl::client_ip(request_from("10.0.0.1", "6.6.6.6, 203.0.113.9"), trusted) == "203.0.113.9");
}

TEST_CASE("client_ip: chained trusted proxies are skipped from the right", "[rate_limit]") {
    const rl::TrustedProxies trusted("10.0.0.0/8");
    REQUIRE(rl::client_ip(request_from("10.0.0.1", "203.0.113.9, 10.1.2.3"), trusted) == "203.0.113.9");
}

TEST_CASE("client_ip: missing or garbage header falls back to the peer", "[rate_limit]") {
    const rl::TrustedProxies trusted("10.0.0.1");
    REQUIRE(rl::client_ip(request_from("10.0.0.1"), trusted) == "10.0.0.1");
    REQUIRE(rl::client_ip(request_from("10.0.0.1", "not-an-ip"), trusted) == "10.0.0.1");
}

TEST_CASE("trusted proxies: CIDR ranges, IPv6, and unparseable entries", "[rate_limit]") {
    const rl::TrustedProxies t("192.168.0.0/16, ::1, fd00::/8, bogus, 10.0.0.0/99");
    REQUIRE(t.contains("192.168.44.2"));
    REQUIRE_FALSE(t.contains("192.169.0.1"));
    REQUIRE(t.contains("::1"));
    REQUIRE(t.contains("fd12:3456::1"));
    REQUIRE_FALSE(t.contains("fe80::1"));
    REQUIRE_FALSE(t.contains("10.0.0.5"));
    REQUIRE_FALSE(t.contains("bogus"));
}

TEST_CASE("by_ip: passes until the bucket is empty, then answers 429 with Retry-After", "[rate_limit]") {
    const auto mw = rl::by_ip(rl::Limiter::make({.burst = 2, .per_second = 0.01}));
    const auto req = request_from("203.0.113.9");

    bool reached = false;
    REQUIRE(run(mw, req, &reached).body == "ok");
    REQUIRE(reached);
    REQUIRE(run(mw, req).body == "ok");

    reached = false;
    const auto refused = run(mw, req, &reached);
    REQUIRE_FALSE(reached);
    REQUIRE(refused.status.code == 429);
    REQUIRE(refused.headers.get("Retry-After") == "100");
    REQUIRE(refused.content_type == "text/plain");

    // Another client is unaffected.
    REQUIRE(run(mw, request_from("203.0.113.10")).body == "ok");
}

TEST_CASE("by_ip: JSON callers get a JSON 429", "[rate_limit]") {
    const auto mw = rl::by_ip(rl::Limiter::make({.burst = 1, .per_second = 0.01}));
    auto req = request_from("203.0.113.9");
    req.path.path = "/api/login";
    run(mw, req);
    const auto refused = run(mw, req);
    REQUIRE(refused.status.code == 429);
    REQUIRE(refused.body.contains("rate limit exceeded"));
    REQUIRE(refused.content_type == "application/json");
}

TEST_CASE("by_ip: requests with no known peer share one bucket", "[rate_limit]") {
    const auto mw = rl::by_ip(rl::Limiter::make({.burst = 1, .per_second = 0.01}));
    REQUIRE(run(mw, request_from("")).body == "ok");
    REQUIRE(run(mw, request_from("")).status.code == 429);
}

TEST_CASE("by_key: keys come from the request and an empty key skips the limit", "[rate_limit]") {
    const auto mw = rl::by_key(rl::Limiter::make({.burst = 1, .per_second = 0.01}),
                               [](const web::Request& r) { return r.query.get("user"); });
    web::Request alice;
    alice.query.data["user"] = "alice";
    web::Request bob;
    bob.query.data["user"] = "bob";
    const web::Request anonymous;

    REQUIRE(run(mw, alice).body == "ok");
    REQUIRE(run(mw, alice).status.code == 429);
    REQUIRE(run(mw, bob).body == "ok");
    REQUIRE(run(mw, anonymous).body == "ok");
    REQUIRE(run(mw, anonymous).body == "ok");
}

TEST_CASE("peer_ip: a TCP connection reports the remote address", "[rate_limit]") {
    const int lfd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(lfd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(lfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    REQUIRE(listen(lfd, 1) == 0);
    socklen_t len = sizeof(addr);
    REQUIRE(getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);

    const int cfd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(connect(cfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    const int sfd = accept(lfd, nullptr, nullptr);
    REQUIRE(sfd >= 0);

    REQUIRE(web::detail::peer_ip(sfd) == "127.0.0.1");

    close(sfd);
    close(cfd);
    close(lfd);
}

TEST_CASE("peer_ip: a non-IP socket gives an empty address", "[rate_limit]") {
    int fds[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    REQUIRE(web::detail::peer_ip(fds[0]).empty());
    close(fds[0]);
    close(fds[1]);
}
