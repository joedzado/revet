// Test cases shared by the SQLite and Postgres token tests. The including file defines AuthDb (a
// fixture with a `pool`, auth_users and auth_sessions created), W and R (Write and Read handle types), count(),
// and AUTH_REQUIRE_DB().
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <revet/auth/tokens.hpp>
#include <set>
#include <thread>
#include <vector>

namespace auth = web::auth;
namespace tok = web::auth::tokens;

namespace {
constexpr long long T0 = 1'800'000'000;

// The secret part of <session>.<generation>.<secret>.<mac>.
auto secret_of(const std::string& token) -> std::string {
    const auto d2 = token.find('.', token.find('.') + 1);
    return token.substr(d2 + 1, token.rfind('.') - d2 - 1);
}
// A body signed the way the server signs refresh tokens, for tests that need well-formed tokens.
auto signed_token(const std::string& body) -> std::string {
    return body + "." + web::auth::tokens::detail::refresh_mac(body);
}

auto make_user(W& w, const std::string& name = "alice") -> std::string {
    return auth::find_or_create_user(w, "test", name);
}

auto bearer_request(const std::string& token) -> web::Request {
    web::Request req;
    req.headers.data["authorization"] = "Bearer " + token;
    return req;
}

// Runs a request through a guard to a handler that reports the principal's user id.
auto call(const web::Middleware& guard, const web::Request& req) -> web::Response {
    return web::dispatch(
        req, [](const web::Request&) { return web::Response{.body = web::api::principal<tok::Principal>().user_id}; },
        {guard});
}
} // namespace

TEST_CASE("tokens: issue gives a working access token and a refresh token", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto user = make_user(w);
    const auto pair = tok::issue(w, user, "Alex's iPhone", T0);

    const auto p = tok::verify_access(pair.access_token, T0 + 10);
    REQUIRE(p.has_value());
    REQUIRE(p->user_id == user);
    REQUIRE(p->session_id == pair.session_id);
    REQUIRE(pair.access_expires_at == T0 + 15 * 60);
    REQUIRE(pair.refresh_expires_at == T0 + 60LL * 24 * 3600);
    REQUIRE(pair.refresh_token.starts_with(pair.session_id + ".1."));
    REQUIRE(tok::session_active(w, pair.session_id, T0));
}

TEST_CASE("tokens: the server stores only a hash of the refresh token", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto pair = tok::issue(w, make_user(w), "", T0);
    const auto secret = secret_of(pair.refresh_token);
    REQUIRE(secret.size() >= 40);
    const auto stored = w.scalar<std::string>("SELECT token_hash FROM auth_sessions WHERE id = ?", pair.session_id);
    REQUIRE(stored.has_value());
    REQUIRE(stored->size() == 64);
    REQUIRE(stored->find(secret) == std::string::npos);
    REQUIRE(*stored != secret);
}

TEST_CASE("tokens: access tokens expire", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto pair = tok::issue(w, make_user(w), "", T0);
    REQUIRE(tok::verify_access(pair.access_token, T0 + 15 * 60 - 1).has_value());
    REQUIRE_FALSE(tok::verify_access(pair.access_token, T0 + 15 * 60).has_value());
    REQUIRE_FALSE(tok::verify_access(pair.access_token, T0 + 86400).has_value());
}

TEST_CASE("tokens: tampered, truncated, empty and foreign access tokens are refused", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto pair = tok::issue(w, make_user(w), "", T0);

    auto flipped = pair.access_token;
    flipped[flipped.size() / 2] = flipped[flipped.size() / 2] == 'A' ? 'B' : 'A';
    REQUIRE_FALSE(tok::verify_access(flipped, T0).has_value());
    REQUIRE_FALSE(tok::verify_access(pair.access_token.substr(0, pair.access_token.size() - 4), T0).has_value());
    REQUIRE_FALSE(tok::verify_access("", T0).has_value());
    REQUIRE_FALSE(tok::verify_access("not a token", T0).has_value());
    REQUIRE_FALSE(tok::verify_access(std::string(2000, 'A'), T0).has_value());
    // Sealed with the right key but not an access token (what a state cookie looks like).
    REQUIRE_FALSE(tok::verify_access(web::crypto::encrypt("some cookie payload"), T0).has_value());
    REQUIRE_FALSE(tok::verify_access(web::crypto::encrypt("revet-at1|u|" + std::string(32, 'a') + "|99999999999x"), T0)
                      .has_value());
    // The refresh token is not an access token.
    REQUIRE_FALSE(tok::verify_access(pair.refresh_token, T0).has_value());
}

TEST_CASE("tokens: refresh rotates the refresh token", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto user = make_user(w);
    const auto first = tok::issue(w, user, "phone", T0);

    const auto second = tok::refresh(w, first.refresh_token, T0 + 100);
    REQUIRE(second.has_value());
    REQUIRE(second->session_id == first.session_id);
    REQUIRE(second->refresh_token != first.refresh_token);
    REQUIRE(second->refresh_token.starts_with(first.session_id + ".2."));
    REQUIRE(second->access_token != first.access_token);
    REQUIRE(second->refresh_expires_at == first.refresh_expires_at); // a session does not extend itself forever
    REQUIRE(tok::verify_access(second->access_token, T0 + 100)->user_id == user);

    const auto third = tok::refresh(w, second->refresh_token, T0 + 200);
    REQUIRE(third.has_value());
    REQUIRE(third->refresh_token.starts_with(first.session_id + ".3."));
}

TEST_CASE("tokens: reusing a rotated refresh token revokes the session", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto first = tok::issue(w, make_user(w), "phone", T0);
    const auto second = tok::refresh(w, first.refresh_token, T0 + 10);
    REQUIRE(second.has_value());

    const auto replay = tok::refresh(w, first.refresh_token, T0 + 20);
    REQUIRE_FALSE(replay.has_value());
    REQUIRE(replay.error() == tok::RefreshError::reuse_detected);

    // The newest token no longer works either: the session is cut off.
    const auto after = tok::refresh(w, second->refresh_token, T0 + 30);
    REQUIRE_FALSE(after.has_value());
    REQUIRE(after.error() == tok::RefreshError::revoked);
    REQUIRE_FALSE(tok::session_active(w, first.session_id, T0 + 30));
}

TEST_CASE("tokens: a forged old-generation token does not revoke the session", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto first = tok::issue(w, make_user(w), "phone", T0);
    const auto second = tok::refresh(w, first.refresh_token, T0 + 10);
    REQUIRE(second.has_value());

    // Someone who knows only the session id tries generation 1 with a made-up secret and MAC.
    const auto forged = tok::refresh(w, first.session_id + ".1.madeupsecret.AAAAAAAAAAAAAAAAAAAAAA", T0 + 20);
    REQUIRE_FALSE(forged.has_value());
    REQUIRE(forged.error() == tok::RefreshError::invalid);
    REQUIRE(tok::session_active(w, first.session_id, T0 + 20));
    REQUIRE(tok::refresh(w, second->refresh_token, T0 + 30).has_value());
}

TEST_CASE("tokens: wrong, malformed and unknown refresh tokens are invalid", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto pair = tok::issue(w, make_user(w), "", T0);
    const auto bad = [&](const std::string& t) {
        const auto r = tok::refresh(w, t, T0 + 1);
        return !r.has_value() && r.error() == tok::RefreshError::invalid;
    };

    const auto secret = secret_of(pair.refresh_token);
    REQUIRE(bad(signed_token(pair.session_id + ".1.wrongsecret")));
    REQUIRE(bad(signed_token(std::string(32, 'a') + ".1." + secret)));
    REQUIRE(bad(signed_token(pair.session_id + ".5." + secret)));               // a generation not issued yet
    REQUIRE(bad(pair.session_id + ".1." + secret));                             // no MAC
    REQUIRE(bad(pair.session_id + ".1." + secret + ".AAAAAAAAAAAAAAAAAAAAAA")); // wrong MAC
    REQUIRE(bad(pair.refresh_token + ".x"));                                    // extra part
    REQUIRE(bad(""));
    REQUIRE(bad("garbage"));
    REQUIRE(bad("a.b.c"));
    REQUIRE(bad(pair.session_id + ".1."));
    REQUIRE(bad(pair.session_id + ".x.secret"));
    REQUIRE(bad(std::string(2000, 'z')));
    // Guessing did not hurt the real token.
    REQUIRE(tok::refresh(w, pair.refresh_token, T0 + 2).has_value());
}

TEST_CASE("tokens: a session ends when refresh_ttl passes", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto pair = tok::issue(w, make_user(w), "", T0);
    const auto late = tok::refresh(w, pair.refresh_token, pair.refresh_expires_at);
    REQUIRE_FALSE(late.has_value());
    REQUIRE(late.error() == tok::RefreshError::expired);
    REQUIRE_FALSE(tok::session_active(w, pair.session_id, pair.refresh_expires_at));
    REQUIRE(tok::session_active(w, pair.session_id, pair.refresh_expires_at - 1));
}

TEST_CASE("tokens: sign out one device leaves the others", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto user = make_user(w);
    const auto phone = tok::issue(w, user, "phone", T0);
    const auto laptop = tok::issue(w, user, "laptop", T0 + 5);

    REQUIRE(tok::list_devices(w, user, T0 + 10).size() == 2);
    REQUIRE(tok::revoke_session(w, user, phone.session_id, T0 + 10));
    REQUIRE_FALSE(tok::revoke_session(w, user, phone.session_id, T0 + 11)); // already ended

    const auto r = tok::refresh(w, phone.refresh_token, T0 + 12);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == tok::RefreshError::revoked);
    REQUIRE(tok::refresh(w, laptop.refresh_token, T0 + 12).has_value());

    const auto devices = tok::list_devices(w, user, T0 + 13);
    REQUIRE(devices.size() == 1);
    REQUIRE(devices[0].session_id == laptop.session_id);
    REQUIRE(devices[0].label == "laptop");
}

TEST_CASE("tokens: a user cannot sign out someone else's device", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto alice = make_user(w, "alice");
    const auto mallory = make_user(w, "mallory");
    const auto pair = tok::issue(w, alice, "phone", T0);
    REQUIRE_FALSE(tok::revoke_session(w, mallory, pair.session_id, T0 + 1));
    REQUIRE(tok::session_active(w, pair.session_id, T0 + 1));
}

TEST_CASE("tokens: sign out everywhere", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto alice = make_user(w, "alice");
    const auto bob = make_user(w, "bob");
    const auto a1 = tok::issue(w, alice, "phone", T0);
    const auto a2 = tok::issue(w, alice, "laptop", T0);
    const auto b1 = tok::issue(w, bob, "phone", T0);

    REQUIRE(tok::revoke_all(w, alice, T0 + 1) == 2);
    REQUIRE(tok::revoke_all(w, alice, T0 + 2) == 0);
    REQUIRE_FALSE(tok::refresh(w, a1.refresh_token, T0 + 3).has_value());
    REQUIRE_FALSE(tok::refresh(w, a2.refresh_token, T0 + 3).has_value());
    REQUIRE(tok::list_devices(w, alice, T0 + 3).empty());
    REQUIRE(tok::refresh(w, b1.refresh_token, T0 + 3).has_value());
}

TEST_CASE("tokens: guard accepts a valid token and exposes the principal", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto user = make_user(w);
    const auto pair = tok::issue(w, user, "", tok::detail::unix_now());

    const auto ok = call(tok::guard(), bearer_request(pair.access_token));
    REQUIRE(ok.status.code == 200);
    REQUIRE(ok.body == user);
}

TEST_CASE("tokens: guard answers 401 to bad, missing and expired tokens", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto expired = tok::issue(w, make_user(w), "", T0); // issued in 2027, "now" is earlier

    REQUIRE(call(tok::guard(), bearer_request("garbage")).status.code == 401);
    REQUIRE(call(tok::guard(), web::Request{}).status.code == 401);
    const auto old = tok::issue(w, make_user(w, "bob"), "", 1000);
    REQUIRE(call(tok::guard(), bearer_request(old.access_token)).status.code == 401);
    (void)expired;
}

TEST_CASE("tokens: guard_checked honours sign-out at once", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    std::string user;
    tok::TokenPair pair;
    {
        W w(fx.pool);
        user = make_user(w);
        pair = tok::issue(w, user, "", tok::detail::unix_now());
    }
    // The guard opens its own read handle per request.
    const auto checked = tok::guard_checked([&fx] { return R(fx.pool); });

    REQUIRE(call(checked, bearer_request(pair.access_token)).status.code == 200);
    {
        W w(fx.pool);
        REQUIRE(tok::revoke_session(w, user, pair.session_id));
    }
    REQUIRE(call(checked, bearer_request(pair.access_token)).status.code == 401);
    // The unchecked guard keeps accepting it until it expires: that is the documented trade.
    REQUIRE(call(tok::guard(), bearer_request(pair.access_token)).status.code == 200);
}

TEST_CASE("tokens: deleting a user removes their sessions", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto user = make_user(w);
    const auto pair = tok::issue(w, user, "phone", T0);
    REQUIRE(auth::delete_user(w, user));
    REQUIRE(count(w, "auth_sessions") == 0);
    const auto r = tok::refresh(w, pair.refresh_token, T0 + 1);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == tok::RefreshError::invalid);
}

TEST_CASE("tokens: purge_ended forgets finished sessions only", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto user = make_user(w);
    const auto live = tok::issue(w, user, "live", T0);
    const auto gone = tok::issue(w, user, "gone", T0);
    tok::revoke_session(w, user, gone.session_id, T0 + 10);

    REQUIRE(tok::purge_ended(w, T0 + 5) == 0);
    REQUIRE(tok::purge_ended(w, T0 + 11) == 1);
    REQUIRE(count(w, "auth_sessions") == 1);
    REQUIRE(tok::session_active(w, live.session_id, T0 + 11));
}

TEST_CASE("tokens: of many racing refreshes with one token, exactly one wins", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    std::string refresh_token;
    {
        W w(fx.pool);
        refresh_token = tok::issue(w, make_user(w), "", tok::detail::unix_now()).refresh_token;
    }
    std::vector<int> won(8, 0);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < won.size(); ++i) {
        threads.emplace_back([&, i] {
            W w(fx.pool);
            won[i] = tok::refresh(w, refresh_token).has_value() ? 1 : 0;
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    int total = 0;
    for (int v : won) {
        total += v;
    }
    REQUIRE(total == 1);
}

TEST_CASE("tokens: lifetimes come from config", "[auth][db]") {
    AUTH_REQUIRE_DB();
    AuthDb fx;
    W w(fx.pool);
    const auto saved = tok::config();
    tok::config().access_ttl = std::chrono::seconds(60);
    tok::config().refresh_ttl = std::chrono::seconds(3600);
    const auto pair = tok::issue(w, make_user(w), "", T0);
    tok::config() = saved;
    REQUIRE(pair.access_expires_at == T0 + 60);
    REQUIRE(pair.refresh_expires_at == T0 + 3600);
}
