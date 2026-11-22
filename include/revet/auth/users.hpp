#pragma once

// Users and the identities that sign them in.
//
// A user is an opaque random id. An identity is one way to prove who that user is: a provider name
// ("passkey", "apple", "google", ...) plus the provider's stable subject. One user can hold several
// identities, and (provider, subject) belongs to exactly one user.
//
// Every function takes the database handle first and works on SQLite and Postgres alike. Functions
// that change data need a db::Write handle and run in their own transaction, so call them on a handle
// that has no transaction open. Tables are named auth_* so they cannot clash with an app's own.
//
//   auto db = resources::main_db<db::Write>();
//   web::auth::create_schema(db);                                  // once at startup
//   const auto id = web::auth::find_or_create_user(db, "google", sub);

#include <array>
#include <ctime>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <sodium.h>

#include "../db.hpp"

namespace web::auth {

struct Identity {
    std::string provider;
    std::string subject;
    long long created_at = 0; // unix seconds
};

enum class LinkResult {
    linked,
    already_linked, // this identity already belongs to this user
    taken,          // this identity belongs to a different user
    no_such_user,
};

enum class UnlinkResult {
    unlinked,
    not_found,
    last_identity, // refused: a user keeps at least one way to sign in
};

namespace detail {

template<typename H>
struct tx_of;
#ifdef REVET_DB_SQLITE
template<>
struct tx_of<db::sqlite::Handle<db::Write>> {
    using type = db::sqlite::Transaction;
};
#endif
#ifdef REVET_DB_POSTGRES
template<>
struct tx_of<db::postgres::Handle<db::Write>> {
    using type = db::postgres::Transaction;
};
#endif

template<typename H>
using Tx = typename tx_of<std::remove_cvref_t<H>>::type;

inline auto now() -> long long {
    return static_cast<long long>(std::time(nullptr));
}

inline auto new_user_id() -> std::string {
    if (sodium_init() < 0) {
        throw std::runtime_error("auth: libsodium failed to initialise");
    }
    std::array<unsigned char, 16> raw{};
    randombytes_buf(raw.data(), raw.size());
    std::array<char, 33> hex{};
    sodium_bin2hex(hex.data(), hex.size(), raw.data(), raw.size());
    return std::string(hex.data());
}

} // namespace detail

// Idempotent. Call once at startup.
template<typename H>
void create_schema(H& h) {
    h.exec("CREATE TABLE IF NOT EXISTS auth_users ("
           " id TEXT PRIMARY KEY,"
           " created_at BIGINT NOT NULL)");
    h.exec("CREATE TABLE IF NOT EXISTS auth_identities ("
           " user_id TEXT NOT NULL REFERENCES auth_users(id) ON DELETE CASCADE,"
           " provider TEXT NOT NULL,"
           " subject TEXT NOT NULL,"
           " created_at BIGINT NOT NULL,"
           " PRIMARY KEY (provider, subject))");
    h.exec("CREATE INDEX IF NOT EXISTS auth_identities_user ON auth_identities (user_id)");
}

template<typename H>
[[nodiscard]] auto user_exists(H& h, std::string_view user_id) -> bool {
    return h.template scalar<long long>("SELECT COUNT(*) FROM auth_users WHERE id = ?", std::string(user_id)).value_or(0) > 0;
}

// The user an identity belongs to, if any.
template<typename H>
[[nodiscard]] auto find_user(H& h, std::string_view provider, std::string_view subject) -> std::optional<std::string> {
    return h.template scalar<std::string>("SELECT user_id FROM auth_identities WHERE provider = ? AND subject = ?",
                                          std::string(provider), std::string(subject));
}

// The user for this identity, creating a new user (and the identity) on first sight.
// Safe when two requests race to create the same identity: both get the same user id.
template<typename H>
auto find_or_create_user(H& h, std::string_view provider, std::string_view subject) -> std::string {
    if (auto existing = find_user(h, provider, subject)) {
        return *existing;
    }
    const auto id = detail::new_user_id();
    const auto now = detail::now();
    detail::Tx<H> tx(h);
    h.exec("INSERT INTO auth_users (id, created_at) VALUES (?, ?)", id, now);
    h.exec("INSERT INTO auth_identities (user_id, provider, subject, created_at) VALUES (?, ?, ?, ?)"
           " ON CONFLICT (provider, subject) DO NOTHING",
           id, std::string(provider), std::string(subject), now);
    auto owner = find_user(h, provider, subject);
    if (!owner) {
        throw std::runtime_error("auth: identity vanished during create");
    }
    if (*owner != id) {
        // Someone else created it between our lookup and our insert: keep theirs.
        h.exec("DELETE FROM auth_users WHERE id = ?", id);
    }
    tx.commit();
    return *owner;
}

// The *_within variants do the same work but open no transaction of their own, for callers that
// need identity changes and their own rows to commit or roll back together. Wrap them in
// a transaction (the handle's Transaction class) or they are not atomic.

// Create a user with a chosen id and one identity. False if that identity already belongs to someone.
template<typename H>
auto create_user_within(H& h, std::string_view user_id, std::string_view provider, std::string_view subject) -> bool {
    if (find_user(h, provider, subject)) {
        return false;
    }
    const auto now = detail::now();
    h.exec("INSERT INTO auth_users (id, created_at) VALUES (?, ?)", std::string(user_id), now);
    h.exec("INSERT INTO auth_identities (user_id, provider, subject, created_at) VALUES (?, ?, ?, ?)"
           " ON CONFLICT (provider, subject) DO NOTHING",
           std::string(user_id), std::string(provider), std::string(subject), now);
    const auto owner = find_user(h, provider, subject);
    return owner && *owner == user_id; // false: another request took the identity first; the caller rolls back
}

template<typename H>
auto link_identity_within(H& h, std::string_view user_id, std::string_view provider, std::string_view subject) -> LinkResult {
    if (!user_exists(h, user_id)) {
        return LinkResult::no_such_user;
    }
    if (const auto owner = find_user(h, provider, subject)) {
        return *owner == user_id ? LinkResult::already_linked : LinkResult::taken;
    }
    h.exec("INSERT INTO auth_identities (user_id, provider, subject, created_at) VALUES (?, ?, ?, ?)"
           " ON CONFLICT (provider, subject) DO NOTHING",
           std::string(user_id), std::string(provider), std::string(subject), detail::now());
    const auto owner = find_user(h, provider, subject);
    if (!owner) {
        throw std::runtime_error("auth: identity vanished during link");
    }
    return *owner == user_id ? LinkResult::linked : LinkResult::taken; // lost a race to another user
}

template<typename H>
auto unlink_identity_within(H& h, std::string_view user_id, std::string_view provider, std::string_view subject) -> UnlinkResult {
    const auto owner = find_user(h, provider, subject);
    if (!owner || *owner != user_id) {
        return UnlinkResult::not_found;
    }
    const auto count = h.template scalar<long long>("SELECT COUNT(*) FROM auth_identities WHERE user_id = ?",
                                                    std::string(user_id)).value_or(0);
    if (count <= 1) {
        return UnlinkResult::last_identity;
    }
    h.exec("DELETE FROM auth_identities WHERE provider = ? AND subject = ?", std::string(provider), std::string(subject));
    return UnlinkResult::unlinked;
}

// Give an existing user another way to sign in.
template<typename H>
auto link_identity(H& h, std::string_view user_id, std::string_view provider, std::string_view subject) -> LinkResult {
    detail::Tx<H> tx(h);
    const auto result = link_identity_within(h, user_id, provider, subject);
    tx.commit();
    return result;
}

template<typename H>
[[nodiscard]] auto identities_of(H& h, std::string_view user_id) -> std::vector<Identity> {
    std::vector<Identity> out;
    for (auto& [provider, subject, created] : h.template query_as<std::string, std::string, long long>(
             "SELECT provider, subject, created_at FROM auth_identities WHERE user_id = ? ORDER BY created_at, provider, subject",
             std::string(user_id))) {
        out.push_back({std::move(provider), std::move(subject), created});
    }
    return out;
}

// Remove one identity. The last remaining identity is kept so the user can still sign in.
template<typename H>
auto unlink_identity(H& h, std::string_view user_id, std::string_view provider, std::string_view subject) -> UnlinkResult {
    detail::Tx<H> tx(h);
    const auto result = unlink_identity_within(h, user_id, provider, subject);
    if (result == UnlinkResult::unlinked) {
        tx.commit();
    }
    return result;
}

// Delete a user and everything that points at them. Other auth tables reference auth_users with
// ON DELETE CASCADE, so sessions and passkeys go too. Returns false if there was no such user.
template<typename H>
auto delete_user(H& h, std::string_view user_id) -> bool {
    detail::Tx<H> tx(h);
    if (!user_exists(h, user_id)) {
        return false;
    }
    h.exec("DELETE FROM auth_identities WHERE user_id = ?", std::string(user_id));
    h.exec("DELETE FROM auth_users WHERE id = ?", std::string(user_id));
    tx.commit();
    return true;
}

} // namespace web::auth
