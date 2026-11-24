#pragma once

// Login sessions for native and API clients: a short-lived access token plus a long-lived refresh
// token, one session per device.
//
//   access token   sealed with REVET_COOKIE_KEY, carries user id, session id and expiry. Checked
//                  with no database read. Sent as `Authorization: Bearer <token>`.
//   refresh token  random, shown to the client once, stored only as a hash. Each use rotates it; using
//                  an old one again means a copy was stolen, so the whole session is revoked. It also
//                  carries a MAC under REVET_COOKIE_KEY, so only a token this server issued can trigger
//                  that revocation: knowing a session id is not enough to sign someone out.
//
//   auto pair = web::auth::tokens::issue(db, user_id, "Alex's iPhone");        // after a sign-in
//   auto next = web::auth::tokens::refresh(db, pair.refresh_token);           // std::expected
//   route_guards() -> { web::auth::tokens::guard() };                         // protect routes
//   web::api::principal<web::auth::tokens::Principal>().user_id               // inside the handler
//
// Revoking a session stops refresh at once; an access token already issued still works until it
// expires (15 minutes by default). Use guard_checked() to look the session up on every request.
// Needs auth_users (see users.hpp) to exist first.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <functional>
#include <optional>
#include <sodium.h>
#include <string>
#include <string_view>
#include <vector>

#include "../crypto.hpp"
#include "../web.hpp"
#include "users.hpp"

namespace web::auth::tokens {

struct Config {
    std::chrono::seconds access_ttl{15 * 60}; // REVET_ACCESS_TTL_SECONDS
    std::chrono::seconds refresh_ttl{60 * 24 *
                                     3600}; // REVET_REFRESH_TTL_SECONDS: a session lasts this long from sign-in
};

// Defaults, overridden by the environment on first use. Assign to the fields to set them in code.
inline auto config() -> Config& {
    static Config cfg = [] {
        Config c;
        auto from_env = [](const char* name, std::chrono::seconds& out) {
            if (const char* v = std::getenv(name); v != nullptr) {
                char* end = nullptr;
                const long n = std::strtol(v, &end, 10);
                if (end != v && *end == '\0' && n > 0) {
                    out = std::chrono::seconds(n);
                }
            }
        };
        from_env("REVET_ACCESS_TTL_SECONDS", c.access_ttl);
        from_env("REVET_REFRESH_TTL_SECONDS", c.refresh_ttl);
        return c;
    }();
    return cfg;
}

struct TokenPair {
    std::string access_token;
    std::string refresh_token;
    std::string session_id;
    long long access_expires_at = 0;  // unix seconds
    long long refresh_expires_at = 0; // unix seconds
};

// Who a verified access token belongs to. Read it in a handler with web::api::principal<Principal>().
struct Principal {
    std::string user_id;
    std::string session_id;
};

struct Device {
    std::string session_id;
    std::string label;
    long long created_at = 0;
    long long last_used_at = 0;
};

enum class RefreshError {
    invalid,        // malformed, unknown session, or wrong secret
    expired,        // the session has outlived refresh_ttl
    revoked,        // signed out
    reuse_detected, // an already-rotated token was presented: the session is now revoked
};

namespace detail {

template<typename H>
using Tx = web::auth::detail::Tx<H>;

constexpr std::string_view k_access_tag = "revet-at1";
constexpr std::size_t k_max_token_len = 512;

inline auto random_hex(std::size_t bytes) -> std::string {
    web::crypto::init();
    std::string raw(bytes, '\0');
    randombytes_buf(raw.data(), raw.size());
    std::string hex(bytes * 2 + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), reinterpret_cast<const unsigned char*>(raw.data()), raw.size());
    hex.pop_back();
    return hex;
}

inline auto random_secret() -> std::string {
    web::crypto::init();
    std::array<unsigned char, 32> raw{};
    randombytes_buf(raw.data(), raw.size());
    std::string out(sodium_base64_ENCODED_LEN(raw.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
    sodium_bin2base64(out.data(), out.size(), raw.data(), raw.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    out.resize(std::strlen(out.c_str()));
    return out;
}

// The server keeps only this. The secret is 256 random bits, so a fast hash is enough.
inline auto hash_secret(std::string_view secret) -> std::string {
    web::crypto::init();
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(digest.data(), reinterpret_cast<const unsigned char*>(secret.data()), secret.size());
    std::string hex(digest.size() * 2 + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
    hex.pop_back();
    return hex;
}

// Keyed BLAKE2b over the token body, under a key derived from REVET_COOKIE_KEY. base64url, 16 bytes.
inline auto refresh_mac(std::string_view body) -> std::string {
    web::crypto::init();
    web::crypto::Key cookie_key{};
    web::crypto::get_key(cookie_key);
    std::array<unsigned char, crypto_generichash_KEYBYTES> key{};
    constexpr std::string_view k_context = "revet-refresh-mac-v1";
    crypto_generichash(key.data(), key.size(), reinterpret_cast<const unsigned char*>(k_context.data()),
                       k_context.size(), cookie_key.data(), cookie_key.size());
    sodium_memzero(cookie_key.data(), cookie_key.size());
    std::array<unsigned char, 16> mac{};
    crypto_generichash(mac.data(), mac.size(), reinterpret_cast<const unsigned char*>(body.data()), body.size(),
                       key.data(), key.size());
    sodium_memzero(key.data(), key.size());
    std::string out(sodium_base64_ENCODED_LEN(mac.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
    sodium_bin2base64(out.data(), out.size(), mac.data(), mac.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    out.resize(std::strlen(out.c_str()));
    return out;
}

// <session id>.<generation>.<secret>.<mac>
inline auto make_refresh_token(std::string_view session_id, long long generation,
                               std::string_view secret) -> std::string {
    auto body = std::string(session_id) + "." + std::to_string(generation) + "." + std::string(secret);
    auto mac = refresh_mac(body);
    return body + "." + mac;
}

struct ParsedRefresh {
    std::string session_id;
    long long generation = 0;
    std::string secret;
};

inline auto is_hex32(std::string_view s) -> bool {
    return s.size() == 32 &&
           std::ranges::all_of(s, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

inline auto parse_refresh_token(std::string_view token) -> std::optional<ParsedRefresh> {
    if (token.size() > k_max_token_len) {
        return std::nullopt;
    }
    const auto d1 = token.find('.');
    if (d1 == std::string_view::npos) {
        return std::nullopt;
    }
    const auto d2 = token.find('.', d1 + 1);
    if (d2 == std::string_view::npos) {
        return std::nullopt;
    }
    const auto d3 = token.find('.', d2 + 1);
    if (d3 == std::string_view::npos || token.find('.', d3 + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    // A token this server did not mint is refused before it can touch the session (and so cannot revoke it).
    if (!web::crypto::secure_eq(refresh_mac(token.substr(0, d3)), token.substr(d3 + 1))) {
        return std::nullopt;
    }
    ParsedRefresh p;
    p.session_id = std::string(token.substr(0, d1));
    const auto gen = token.substr(d1 + 1, d2 - d1 - 1);
    p.secret = std::string(token.substr(d2 + 1, d3 - d2 - 1));
    if (!is_hex32(p.session_id) || gen.empty() || gen.size() > 12 || p.secret.empty()) {
        return std::nullopt;
    }
    long long g = 0;
    for (char c : gen) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        g = g * 10 + (c - '0');
    }
    p.generation = g;
    return p;
}

inline auto make_access_token(const Principal& p, long long expires_at) -> std::string {
    return web::crypto::encrypt(std::string(k_access_tag) + "|" + p.user_id + "|" + p.session_id + "|" +
                                std::to_string(expires_at));
}

inline auto unix_now() -> long long {
    return web::auth::detail::now();
}

} // namespace detail

// Idempotent. Call once at startup, after users::create_schema.
template<typename H>
void create_schema(H& h) {
    h.exec("CREATE TABLE IF NOT EXISTS auth_sessions ("
           " id TEXT PRIMARY KEY,"
           " user_id TEXT NOT NULL REFERENCES auth_users(id) ON DELETE CASCADE,"
           " token_hash TEXT NOT NULL,"
           " generation BIGINT NOT NULL,"
           " label TEXT NOT NULL,"
           " created_at BIGINT NOT NULL,"
           " last_used_at BIGINT NOT NULL,"
           " expires_at BIGINT NOT NULL,"
           " revoked_at BIGINT)");
    h.exec("CREATE INDEX IF NOT EXISTS auth_sessions_user ON auth_sessions (user_id)");
}

// Check an access token. No database read. Expired, tampered, or foreign tokens give nullopt.
[[nodiscard]] inline auto verify_access(std::string_view token,
                                        long long now = detail::unix_now()) -> std::optional<Principal> {
    if (token.empty() || token.size() > detail::k_max_token_len) {
        return std::nullopt;
    }
    const auto plain = web::crypto::decrypt(token);
    std::string_view rest = plain;
    auto next = [&rest]() -> std::optional<std::string_view> {
        const auto bar = rest.find('|');
        if (bar == std::string_view::npos) {
            return std::nullopt;
        }
        const auto part = rest.substr(0, bar);
        rest.remove_prefix(bar + 1);
        return part;
    };
    const auto tag = next();
    const auto user = next();
    const auto session = next();
    if (!tag || !user || !session || *tag != detail::k_access_tag || user->empty() || !detail::is_hex32(*session)) {
        return std::nullopt;
    }
    long long exp = 0;
    if (rest.empty() || rest.size() > 18) {
        return std::nullopt;
    }
    for (char c : rest) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        exp = exp * 10 + (c - '0');
    }
    if (exp <= now) {
        return std::nullopt;
    }
    return Principal{std::string(*user), std::string(*session)};
}

// Start a session for a user who has just proven who they are.
template<typename H>
auto issue(H& h, std::string_view user_id, std::string_view device_label = "",
           long long now = detail::unix_now()) -> TokenPair {
    const auto cfg = config();
    const auto session_id = detail::random_hex(16);
    const auto secret = detail::random_secret();
    const long long refresh_exp = now + cfg.refresh_ttl.count();
    h.exec(
        "INSERT INTO auth_sessions (id, user_id, token_hash, generation, label, created_at, last_used_at, expires_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        session_id, std::string(user_id), detail::hash_secret(secret), 1LL, std::string(device_label), now, now,
        refresh_exp);
    const long long access_exp = now + cfg.access_ttl.count();
    return {.access_token = detail::make_access_token({std::string(user_id), session_id}, access_exp),
            .refresh_token = detail::make_refresh_token(session_id, 1, secret),
            .session_id = session_id,
            .access_expires_at = access_exp,
            .refresh_expires_at = refresh_exp};
}

// Trade a refresh token for a new pair. The presented token stops working. Presenting a token that
// was already traded revokes the session, because two parties now hold copies.
template<typename H>
auto refresh(H& h, std::string_view refresh_token,
             long long now = detail::unix_now()) -> std::expected<TokenPair, RefreshError> {
    const auto parsed = detail::parse_refresh_token(refresh_token);
    if (!parsed) {
        return std::unexpected(RefreshError::invalid);
    }

    detail::Tx<H> tx(h);
    const auto rows = h.template query_as<std::string, std::string, long long, long long, std::optional<long long>>(
        "SELECT user_id, token_hash, generation, expires_at, revoked_at FROM auth_sessions WHERE id = ?",
        parsed->session_id);
    if (rows.empty()) {
        return std::unexpected(RefreshError::invalid);
    }
    const auto& [user_id, token_hash, generation, expires_at, revoked_at] = rows.front();

    if (revoked_at) {
        return std::unexpected(RefreshError::revoked);
    }
    if (expires_at <= now) {
        return std::unexpected(RefreshError::expired);
    }

    if (parsed->generation < generation) {
        // The caller holds a token we already replaced. Cut the session off, and keep that change.
        h.exec("UPDATE auth_sessions SET revoked_at = ? WHERE id = ? AND revoked_at IS NULL", now, parsed->session_id);
        tx.commit();
        return std::unexpected(RefreshError::reuse_detected);
    }
    if (parsed->generation != generation || !web::crypto::secure_eq(detail::hash_secret(parsed->secret), token_hash)) {
        return std::unexpected(RefreshError::invalid);
    }

    const auto secret = detail::random_secret();
    // Compare-and-swap on the generation so two racing refreshes cannot both win.
    const auto won = h.template scalar<long long>(
        "UPDATE auth_sessions SET token_hash = ?, generation = generation + 1, last_used_at = ?"
        " WHERE id = ? AND generation = ? AND revoked_at IS NULL RETURNING generation",
        detail::hash_secret(secret), now, parsed->session_id, generation);
    if (!won) {
        return std::unexpected(RefreshError::invalid);
    }
    tx.commit();

    const long long access_exp = now + config().access_ttl.count();
    return TokenPair{.access_token = detail::make_access_token({user_id, parsed->session_id}, access_exp),
                     .refresh_token = detail::make_refresh_token(parsed->session_id, *won, secret),
                     .session_id = parsed->session_id,
                     .access_expires_at = access_exp,
                     .refresh_expires_at = expires_at};
}

// True while the session exists, is not revoked, and has not outlived refresh_ttl.
template<typename H>
[[nodiscard]] auto session_active(H& h, std::string_view session_id, long long now = detail::unix_now()) -> bool {
    return h.template scalar<long long>(
                "SELECT COUNT(*) FROM auth_sessions WHERE id = ? AND revoked_at IS NULL AND expires_at > ?",
                std::string(session_id), now)
               .value_or(0) > 0;
}

// Sign out one device. Scoped to the user, so one user cannot end another's session.
template<typename H>
auto revoke_session(H& h, std::string_view user_id, std::string_view session_id,
                    long long now = detail::unix_now()) -> bool {
    return !h.query("UPDATE auth_sessions SET revoked_at = ? WHERE id = ? AND user_id = ? AND revoked_at IS NULL "
                    "RETURNING id",
                    now, std::string(session_id), std::string(user_id))
                .empty();
}

// Sign out everywhere. Returns how many sessions were ended.
template<typename H>
auto revoke_all(H& h, std::string_view user_id, long long now = detail::unix_now()) -> std::size_t {
    return h
        .query("UPDATE auth_sessions SET revoked_at = ? WHERE user_id = ? AND revoked_at IS NULL RETURNING id", now,
               std::string(user_id))
        .size();
}

// The user's signed-in devices, most recently used first.
template<typename H>
[[nodiscard]] auto list_devices(H& h, std::string_view user_id,
                                long long now = detail::unix_now()) -> std::vector<Device> {
    std::vector<Device> out;
    for (auto& [id, label, created, used] : h.template query_as<std::string, std::string, long long, long long>(
             "SELECT id, label, created_at, last_used_at FROM auth_sessions"
             " WHERE user_id = ? AND revoked_at IS NULL AND expires_at > ? ORDER BY last_used_at DESC, id",
             std::string(user_id), now)) {
        out.push_back({std::move(id), std::move(label), created, used});
    }
    return out;
}

// Forget sessions that ended (revoked or expired) before `before`. Run now and then.
template<typename H>
auto purge_ended(H& h, long long before) -> std::size_t {
    return h.query("DELETE FROM auth_sessions WHERE expires_at < ? OR revoked_at < ? RETURNING id", before, before)
        .size();
}

// Route guard: 401 unless the request carries a valid access token. No database read.
inline auto guard() -> Middleware {
    return web::api::bearer<Principal>(std::function<std::optional<Principal>(std::string_view)>(
        [](std::string_view token) { return verify_access(token); }));
}

// As guard(), and the session must still be active in the database, so sign-out takes effect at once.
// `open` returns a database handle, e.g. [] { return resources::main_db<db::Read>(); }.
template<typename Open>
auto guard_checked(Open open) -> Middleware {
    return web::api::bearer<Principal>(std::function<std::optional<Principal>(std::string_view)>(
        [open = std::move(open)](std::string_view token) -> std::optional<Principal> {
            auto p = verify_access(token);
            if (!p) {
                return std::nullopt;
            }
            auto h = open();
            if (!session_active(h, p->session_id)) {
                return std::nullopt;
            }
            return p;
        }));
}

} // namespace web::auth::tokens
