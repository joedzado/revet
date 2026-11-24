#pragma once

// Magic-link sign-in: type an email address, get a link, click it, you are in. No password to set or
// forget. Following the link proves the person controls the address, so it also makes the account.
//
//   web::auth::magic::config().base_url = "https://app.example.com";     // or REVET_BASE_URL
//   web::auth::magic::create_schema(db);
//
//   magic::request(db, "ann@example.com");              // mails a link (always the same answer)
//   auto done = magic::complete(db, token_from_link);   // user id, or invalid_link
//
// The same flow adds an email address to a signed-in user (`Purpose::link`), which is also how a user
// who lost every passkey gets back in: their address is an identity (provider "email").
//
// Design points:
//  * The link carries a random 256-bit token. Only its SHA-256 is stored. It works once and expires
//    (15 minutes by default).
//  * The link is built from REVET_BASE_URL, never from the request's Host header, so a forged Host
//    cannot make us mail a link to somebody else's server.
//  * request() answers the same whether or not the address has an account, and says nothing about
//    whether a mail was really sent, so it cannot be used to find out who is registered.
//  * Mail sending is rate limited three ways: the HTTP guards (per address and per client), a cap per
//    email address per hour here in the database, and a global cap in limits.hpp.
//  * Mail scanners that fetch every link in a message must not use it up: the link opens a page with a
//    button, and the button's POST is what calls complete(). The JSON API for apps is the same: the app
//    receives the link, then POSTs the token.

#include <array>
#include <chrono>
#include <cstdlib>
#include <expected>
#include <functional>
#include <optional>
#include <sodium.h>
#include <string>
#include <string_view>

#include "../mail.hpp"
#include "../webauthn.hpp"
#include "users.hpp"

namespace web::auth::magic {

struct Config {
    std::string base_url;                    // REVET_BASE_URL: "https://app.example.com", no trailing slash
    std::string path = "/auth/magic";        // where the link lands; the page reads ?token=
    std::string app_name = "App";            // REVET_APP_NAME: appears in the mail
    std::chrono::seconds ttl{15 * 60};       // REVET_MAGIC_TTL_SECONDS
    int max_per_address_per_hour = 5;        // links made for one address; more are quietly not sent
    std::function<void(mail::Message)> send; // empty: web::mail::outbox()
};

inline auto config() -> Config& {
    static Config c = [] {
        Config cfg;
        if (const char* v = std::getenv("REVET_BASE_URL")) {
            cfg.base_url = v;
        }
        if (const char* v = std::getenv("REVET_APP_NAME")) {
            cfg.app_name = v;
        }
        if (const char* v = std::getenv("REVET_MAGIC_TTL_SECONDS")) {
            char* end = nullptr;
            const long n = std::strtol(v, &end, 10);
            if (end != v && *end == '\0' && n > 0) {
                cfg.ttl = std::chrono::seconds(n);
            }
        }
        while (!cfg.base_url.empty() && cfg.base_url.back() == '/') {
            cfg.base_url.pop_back();
        }
        return cfg;
    }();
    return c;
}

enum class Purpose { sign_in, link };

enum class RequestResult {
    sent,          // a mail was queued (or, for a throttled address, quietly not: see throttled)
    throttled,     // too many links for this address this hour; nothing was sent
    invalid_email, // not an address we can mail
    no_such_user,  // Purpose::link for a user that does not exist
};

enum class CompleteError {
    invalid_link, // unknown, expired, used, or malformed: deliberately one answer
    email_taken,  // Purpose::link, and the address already belongs to another user
};

struct Completed {
    std::string user_id;
    std::string email;
    bool new_user = false; // the link created the account
    bool linked = false;   // Purpose::link: the address was added to a user
};

namespace detail {

inline auto sha256_hex(std::string_view s) -> std::string {
    if (sodium_init() < 0) {
        throw std::runtime_error("auth: libsodium failed to initialise");
    }
    std::array<unsigned char, crypto_hash_sha256_BYTES> d{};
    crypto_hash_sha256(d.data(), reinterpret_cast<const unsigned char*>(s.data()), s.size());
    std::string hex(d.size() * 2 + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), d.data(), d.size());
    hex.pop_back();
    return hex;
}

inline auto new_token() -> std::string {
    if (sodium_init() < 0) {
        throw std::runtime_error("auth: libsodium failed to initialise");
    }
    std::string raw(32, '\0');
    randombytes_buf(raw.data(), raw.size());
    return webauthn::base64url_encode(raw);
}

inline auto html_escape(std::string_view s) -> std::string {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                out += c;
        }
    }
    return out;
}

// https only, except for a server on this machine (development).
inline auto checked_base_url() -> std::string {
    const auto& u = config().base_url;
    const bool https = u.starts_with("https://") && u.size() > 8;
    const bool local = u.starts_with("http://localhost") || u.starts_with("http://127.0.0.1");
    if (!https && !local) {
        throw std::runtime_error("auth: REVET_BASE_URL must be set to the site's https:// address for magic links");
    }
    if (u.find_first_of(" \r\n\t\"'<>") != std::string::npos) {
        throw std::runtime_error("auth: REVET_BASE_URL contains unsafe characters");
    }
    return u;
}

inline auto build_message(const std::string& to, const std::string& link, Purpose purpose) -> mail::Message {
    const auto& cfg = config();
    const auto minutes = std::max<long long>(1, cfg.ttl.count() / 60);
    const std::string name = cfg.app_name;
    const bool adding = purpose == Purpose::link;
    const std::string subject = adding ? "Confirm your email address for " + name : "Your " + name + " sign-in link";
    const std::string lead =
        adding ? "Confirm this email address for your " + name + " account:" : "Sign in to " + name + ":";
    mail::Message m;
    m.to = to;
    m.subject = subject;
    m.text = lead + "\n\n" + link + "\n\nThe link works once and expires in " + std::to_string(minutes) +
             " minutes.\nIf you did not ask for it, you can ignore this message; nothing happens until the link is "
             "opened.\n";
    m.html = "<p>" + html_escape(lead) + "</p><p><a href=\"" + html_escape(link) + "\">" +
             html_escape(adding ? "Confirm email address" : "Sign in") +
             "</a></p><p>The link works once and expires in " + std::to_string(minutes) +
             " minutes.<br>If you did not ask for it, you can ignore this message; nothing happens until the link is "
             "opened.</p>";
    return m;
}

} // namespace detail

// Idempotent. Call once at startup, after users::create_schema.
template<typename H>
void create_schema(H& h) {
    h.exec("CREATE TABLE IF NOT EXISTS auth_magic_links ("
           " token_hash TEXT PRIMARY KEY,"
           " email TEXT NOT NULL,"
           " purpose TEXT NOT NULL,"
           " user_id TEXT NOT NULL,"
           " created_at BIGINT NOT NULL,"
           " expires_at BIGINT NOT NULL,"
           " used_at BIGINT)");
    h.exec("CREATE INDEX IF NOT EXISTS auth_magic_links_email ON auth_magic_links (email, created_at)");
}

// Mail a link to `email`. For Purpose::link, `user_id` is the signed-in user the address will join.
// Valid addresses all get `sent`, whether or not they have an account.
template<typename H>
auto request(H& h, std::string_view email_raw, Purpose purpose = Purpose::sign_in, std::string_view user_id = {},
             long long now = web::auth::detail::now()) -> RequestResult {
    const auto email = mail::normalize_address(email_raw);
    if (!email) {
        return RequestResult::invalid_email;
    }
    if (purpose == Purpose::link && !user_exists(h, user_id)) {
        return RequestResult::no_such_user;
    }
    const auto base =
        detail::checked_base_url(); // throws on a bad deployment: better a 500 than a mail with a wrong link

    const auto recent =
        h.template scalar<long long>("SELECT COUNT(*) FROM auth_magic_links WHERE email = ? AND created_at > ?", *email,
                                     now - 3600)
            .value_or(0);
    if (recent >= config().max_per_address_per_hour) {
        return RequestResult::throttled;
    }

    h.exec("DELETE FROM auth_magic_links WHERE expires_at < ?",
           now - 86400); // keep used and expired rows a day for forensics
    const auto token = detail::new_token();
    h.exec("INSERT INTO auth_magic_links (token_hash, email, purpose, user_id, created_at, expires_at, used_at) VALUES "
           "(?, ?, ?, ?, ?, ?, NULL)",
           detail::sha256_hex(token), *email, std::string(purpose == Purpose::link ? "link" : "sign_in"),
           std::string(user_id), now, now + config().ttl.count());

    const auto link = base + config().path + "?token=" + token;
    auto msg = detail::build_message(*email, link, purpose);
    if (config().send) {
        config().send(std::move(msg));
    } else {
        mail::outbox().enqueue(std::move(msg));
    }
    return RequestResult::sent;
}

// What a token would do, without spending it: the address and purpose, or nothing if it is unknown, used
// or expired. For the page that asks "sign in as ann@example.com?" before the button is pressed.
struct Peeked {
    std::string email;
    Purpose purpose = Purpose::sign_in;
};

template<typename H>
auto peek(H& h, std::string_view token, long long now = web::auth::detail::now()) -> std::optional<Peeked> {
    if (token.empty() || token.size() > 128) {
        return std::nullopt;
    }
    const auto rows = h.template query_as<std::string, std::string>(
        "SELECT email, purpose FROM auth_magic_links WHERE token_hash = ? AND used_at IS NULL AND expires_at > ?",
        detail::sha256_hex(token), now);
    if (rows.empty()) {
        return std::nullopt;
    }
    return Peeked{std::get<0>(rows.front()), std::get<1>(rows.front()) == "link" ? Purpose::link : Purpose::sign_in};
}

// Spend a token. A sign-in token signs in the address's user, creating the account if the address is new.
// A link token adds the address to the user it was requested for.
template<typename H>
auto complete(H& h, std::string_view token,
              long long now = web::auth::detail::now()) -> std::expected<Completed, CompleteError> {
    if (token.empty() || token.size() > 128) {
        return std::unexpected(CompleteError::invalid_link);
    }
    web::auth::detail::Tx<H> tx(h);
    const auto rows = h.template query_as<std::string, std::string, std::string>(
        "UPDATE auth_magic_links SET used_at = ? WHERE token_hash = ? AND used_at IS NULL AND expires_at > ? RETURNING "
        "email, purpose, user_id",
        now, detail::sha256_hex(token), now);
    if (rows.empty()) {
        return std::unexpected(CompleteError::invalid_link);
    }
    const auto& [email, purpose, owner] = rows.front();

    Completed out;
    out.email = email;
    if (purpose == "link") {
        switch (web::auth::link_identity_within(h, owner, "email", email)) {
            case LinkResult::linked:
            case LinkResult::already_linked:
                break;
            case LinkResult::taken:
                tx.commit(); // the token is spent either way
                return std::unexpected(CompleteError::email_taken);
            case LinkResult::no_such_user:
                tx.commit();
                return std::unexpected(CompleteError::invalid_link);
        }
        out.user_id = owner;
        out.linked = true;
    } else if (const auto existing = find_user(h, "email", email)) {
        out.user_id = *existing;
    } else {
        out.user_id = web::auth::detail::new_user_id();
        if (!create_user_within(h, out.user_id, "email", email)) {
            return std::unexpected(CompleteError::invalid_link); // lost a race: roll back, try again
        }
        out.new_user = true;
    }
    tx.commit();
    return out;
}

} // namespace web::auth::magic
