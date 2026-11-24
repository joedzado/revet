// Randomised model check of the token layer: thousands of random operations against a simple
// in-memory model of what should happen. The including file defines AuthDb, W, count() and
// AUTH_REQUIRE_DB() as in the other shared cases.
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <random>
#include <revet/auth/tokens.hpp>
#include <set>
#include <vector>

// NOLINTBEGIN(cert-dcl59-cpp,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,google-build-namespaces,misc-anonymous-namespace-in-header):

// test fixtures

namespace auth = web::auth;
namespace tok = web::auth::tokens;

namespace {

struct ModelSession {
    std::string user;
    std::string current_refresh;
    std::vector<std::string> old_refresh;
    std::vector<std::pair<std::string, long long>> access; // token, expiry
    long long expires_at = 0;
    bool revoked = false;
};

struct Model {
    std::map<std::string, ModelSession> sessions; // by session id
    std::map<std::string, std::string> users;     // name -> id
};

void run_model(unsigned seed, int steps) {
    AuthDb fx;
    W w(fx.pool);
    std::mt19937_64 rng(seed);
    Model m;
    std::vector<std::string> names = {"ann", "bob", "cy", "di"};
    for (const auto& n : names) {
        m.users[n] = auth::find_or_create_user(w, "model", n);
    }
    long long now = 1'800'000'000;

    const auto pick_session = [&] -> ModelSession* {
        if (m.sessions.empty()) {
            return nullptr;
        }
        auto it = m.sessions.begin();
        std::advance(it, static_cast<long>(rng() % m.sessions.size()));
        return &it->second;
    };
    const auto sid_of = [&](const ModelSession* s) -> std::string {
        for (const auto& [id, v] : m.sessions) {
            if (&v == s) {
                return id;
            }
        }
        return {};
    };
    const auto active = [&](const ModelSession& s) -> bool { return !s.revoked && s.expires_at > now; };

    for (int step = 0; step < steps; ++step) {
        now += static_cast<long long>(rng() % 400); // time moves; some sessions will expire
        if (rng() % 50 == 0) {
            now += 20LL * 24 * 3600;
        }
        const auto op = rng() % 12;
        INFO("seed " << seed << " step " << step << " op " << op << " now " << now);

        if (op <= 1 || m.sessions.empty()) { // issue
            const auto& name = names[rng() % names.size()];
            if (!auth::user_exists(w, m.users[name])) {
                m.users[name] = auth::find_or_create_user(w, "model", name);
            }
            const auto pair = tok::issue(w, m.users[name], "dev", now);
            ModelSession s;
            s.user = m.users[name];
            s.current_refresh = pair.refresh_token;
            s.access.emplace_back(pair.access_token, pair.access_expires_at);
            s.expires_at = pair.refresh_expires_at;
            m.sessions[pair.session_id] = s;
        } else if (op <= 4) { // refresh with the current token
            auto* s = pick_session();
            const auto sid = sid_of(s);
            const auto r = tok::refresh(w, s->current_refresh, now);
            if (s->revoked) {
                REQUIRE_FALSE(r.has_value());
                REQUIRE(r.error() == tok::RefreshError::revoked);
            } else if (s->expires_at <= now) {
                REQUIRE_FALSE(r.has_value());
                REQUIRE(r.error() == tok::RefreshError::expired);
            } else {
                REQUIRE(r.has_value());
                REQUIRE(r->session_id == sid);
                REQUIRE(r->refresh_token != s->current_refresh);
                s->old_refresh.push_back(s->current_refresh);
                s->current_refresh = r->refresh_token;
                s->access.emplace_back(r->access_token, r->access_expires_at);
            }
        } else if (op == 5) { // replay an old refresh token
            auto* s = pick_session();
            if (s->old_refresh.empty()) {
                continue;
            }
            const auto& old = s->old_refresh[rng() % s->old_refresh.size()];
            const auto r = tok::refresh(w, old, now);
            REQUIRE_FALSE(r.has_value());
            if (s->revoked) {
                REQUIRE(r.error() == tok::RefreshError::revoked);
            } else if (s->expires_at <= now) {
                REQUIRE(r.error() == tok::RefreshError::expired);
            } else {
                REQUIRE(r.error() == tok::RefreshError::reuse_detected);
                s->revoked = true;
            }
        } else if (op == 6) { // garbage and tampered refresh tokens
            const auto* s = pick_session();
            auto bad = s->current_refresh;
            bad[bad.size() - 1] = bad.back() == 'A' ? 'B' : 'A';
            const auto r1 = tok::refresh(w, bad, now);
            REQUIRE_FALSE(r1.has_value());
            REQUIRE(r1.error() == tok::RefreshError::invalid); // the MAC fails before the session is looked at
            REQUIRE_FALSE(tok::refresh(w, "garbage", now).has_value());
        } else if (op == 7) { // sign out one device
            auto* s = pick_session();
            const auto sid = sid_of(s);
            const bool ended = tok::revoke_session(w, s->user, sid, now);
            REQUIRE(ended == !s->revoked);
            s->revoked = true;
        } else if (op == 8) { // sign out everywhere
            const auto& name = names[rng() % names.size()];
            std::size_t expected = 0;
            for (auto& [id, s] : m.sessions) {
                if (s.user == m.users[name] && !s.revoked) {
                    ++expected;
                    s.revoked = true;
                }
            }
            REQUIRE(tok::revoke_all(w, m.users[name], now) == expected);
        } else if (op == 9) { // access tokens
            auto* s = pick_session();
            const auto& [token, exp] = s->access[rng() % s->access.size()];
            const auto p = tok::verify_access(token, now);
            REQUIRE(p.has_value() == (now < exp));
            if (p) {
                REQUIRE(p->user_id == s->user);
                REQUIRE(p->session_id == sid_of(s));
            }
        } else if (op == 10) { // delete a user, who is then recreated lazily
            const auto& name = names[rng() % names.size()];
            REQUIRE(auth::delete_user(w, m.users[name]));
            for (auto it = m.sessions.begin(); it != m.sessions.end();) {
                if (it->second.user == m.users[name]) {
                    it = m.sessions.erase(it);
                } else {
                    ++it;
                }
            }
            m.users[name] = auth::find_or_create_user(w, "model", name);
        } else { // purge
            const auto cutoff = now - static_cast<long long>(rng() % 1000);
            tok::purge_ended(w, cutoff);
            // Purged sessions are gone from the database; anything the model still tracks as ended
            // simply answers "invalid" instead of "revoked"/"expired" from here on, so forget them.
            for (auto it = m.sessions.begin(); it != m.sessions.end();) {
                const auto row =
                    w.scalar<long long>("SELECT COUNT(*) FROM auth_sessions WHERE id = ?", it->first).value_or(0);
                if (row == 0) {
                    it = m.sessions.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // After every step the database and the model agree on who is signed in where.
        if (step % 7 == 0) {
            for (const auto& name : names) {
                std::size_t expected = 0;
                for (const auto& [id, s] : m.sessions) {
                    if (s.user == m.users[name] && active(s)) {
                        ++expected;
                    }
                }
                REQUIRE(tok::list_devices(w, m.users[name], now).size() == expected);
            }
            for (const auto& [id, s] : m.sessions) {
                REQUIRE(tok::session_active(w, id, now) == active(s));
            }
        }
    }
}

} // namespace

TEST_CASE("model: random token operations match a simple model", "[auth][db][model]") {
    AUTH_REQUIRE_DB();
    for (const unsigned seed : {1U, 2U, 3U, 4U, 5U}) {
        run_model(seed, 1500);
    }
}
// NOLINTEND(cert-dcl59-cpp,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,google-build-namespaces,misc-anonymous-namespace-in-header)
