#pragma once

// Passkey login: registration and sign-in ceremonies on top of webauthn.hpp, with credentials,
// challenges and the link to a user stored in the database. The HTTP layer is in passkeys_http.hpp.
//
//   web::auth::passkeys::config().rp = {.rp_id = "app.example.com", .origins = {"https://app.example.com"}};
//   web::auth::passkeys::create_schema(db);                       // after users and tokens schemas
//
//   // Registration (a new account, or another passkey for a signed-in user)
//   auto opts = begin_registration(db, std::nullopt, "Alex");      // send opts->options_json to the app
//   auto reg  = finish_registration(db, opts->challenge_id, client_data_json, attestation_object, "iPhone",
//                                   Expect::new_account());   // or Expect::user(id) when adding a passkey
//
//   // Sign in (no username: the app picks a passkey from the device)
//   auto req  = begin_login(db);
//   auto who  = finish_login(db, req.challenge_id, credential_id, client_data_json, authenticator_data, signature);
//
// Every passkey is also an identity (provider "passkey", subject = credential id), so the users.hpp
// rule that a user keeps at least one way to sign in covers passkeys too.
//
// Challenges are single use: finish_* consumes the challenge whether or not verification succeeds.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <optional>
#include <sodium.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../crypto.hpp"
#include "../json_write.hpp"
#include "../webauthn.hpp"
#include "users.hpp"

namespace web::auth::passkeys {

struct AndroidApp {
    std::string package_name;                          // "com.example.app"
    std::vector<std::string> sha256_cert_fingerprints; // "AA:BB:...", of the app's signing certificate
};

struct Config {
    std::string rp_name = "App";                // shown by the OS in the passkey sheet
    webauthn::Config rp;                        // rp_id, origins, require_user_verification
    std::chrono::seconds challenge_ttl{5 * 60}; // how long a started ceremony stays open
    std::vector<std::string> apple_app_ids;     // "TEAMID.com.example.app": served in apple-app-site-association
    std::vector<AndroidApp> android_apps;       // served in assetlinks.json
    std::vector<std::string> app_link_paths{
        "/auth/magic"}; // paths the native apps open themselves (universal / app links), for magic links
};

// Defaults, overridden by the environment on first use: REVET_RP_ID, REVET_RP_NAME,
// REVET_WEBAUTHN_ORIGINS (comma-separated). Assign to the fields to set them in code.
inline auto config() -> Config& {
    static Config cfg = [] -> Config {
        Config c;
        if (const char* v = std::getenv("REVET_RP_ID")) {
            c.rp.rp_id = v;
        }
        if (const char* v = std::getenv("REVET_RP_NAME")) {
            c.rp_name = v;
        }
        if (const char* v = std::getenv("REVET_WEBAUTHN_ORIGINS")) {
            std::string_view rest = v;
            while (!rest.empty()) {
                const auto comma = rest.find(',');
                auto item = rest.substr(0, comma);
                while (!item.empty() && item.front() == ' ') {
                    item.remove_prefix(1);
                }
                while (!item.empty() && item.back() == ' ') {
                    item.remove_suffix(1);
                }
                if (!item.empty()) {
                    c.rp.origins.emplace_back(item);
                }
                if (comma == std::string_view::npos) {
                    break;
                }
                rest.remove_prefix(comma + 1);
            }
        }
        return c;
    }();
    return cfg;
}

struct Failure {
    enum class Kind : std::uint8_t {
        bad_request,         // a field is missing or not base64url
        challenge_invalid,   // unknown, expired, already used, or for another ceremony
        verification_failed, // the authenticator's answer did not check out; see `detail`
        unknown_credential,  // no passkey with that id
        credential_taken,    // registering a passkey that already exists
        user_mismatch,       // login: the user handle is not the owner; registration: not the expected caller
        no_such_user,        // adding a passkey to a user that does not exist
    };
    Kind kind;
    std::optional<webauthn::Error> detail; // set for verification_failed
};

struct Options {
    std::string challenge_id; // the app sends this back with its answer
    std::string options_json; // PublicKeyCredential creation or request options (WebAuthn JSON form)
};

struct Registered {
    std::string user_id;
    std::string credential_id; // base64url
    bool new_user = false;
};

// Which ceremony the caller may finish. finish_registration refuses any other challenge with
// user_mismatch before anything is stored (the challenge is spent, like any tried challenge).
//
// A challenge id is a secret, but it is only a bearer secret: without this, whoever presents it
// finishes it, signed in as someone else or not signed in at all. A route that knows who is calling
// should say so:
//   signed in            -> Expect::user(their id): only a challenge issued for them to add a passkey
//   not signed in        -> Expect::new_account(): only a challenge that creates an account
//   checked another way  -> Expect::any() (the default)
class Expect {
public:
    static auto any() -> Expect {
        return {Mode::any, {}};
    }
    static auto new_account() -> Expect {
        return {Mode::new_account, {}};
    }
    // An empty id matches nothing, so a missing id cannot switch the check off.
    static auto user(std::string_view id) -> Expect {
        return {Mode::user, std::string(id)};
    }

    // `challenge_user` is the user id the challenge was issued for (a fresh random one for a new account).
    [[nodiscard]] auto permits(bool challenge_is_new_account, std::string_view challenge_user) const -> bool {
        switch (mode_) {
            case Mode::any:
                return true;
            case Mode::new_account:
                return challenge_is_new_account;
            case Mode::user:
                return !challenge_is_new_account && !user_.empty() && user_ == challenge_user;
        }
        return false;
    }

private:
    enum class Mode : std::uint8_t { any, new_account, user };
    Expect(Mode m, std::string user) : mode_(m), user_(std::move(user)) {}
    Mode mode_;
    std::string user_;
};

struct LoggedIn {
    std::string user_id;
    std::string credential_id; // base64url
};

struct PasskeyInfo {
    std::string credential_id; // base64url
    std::string label;
    long long created_at = 0;
    long long last_used_at = 0; // 0 = never signed in with it
    bool backup_eligible = false;
    bool backup_state = false;
};

enum class RemoveResult : std::uint8_t {
    removed,
    not_found,
    last_login_method, // refused: the user would be locked out
};

namespace detail {

template<typename H>
using Tx = web::auth::detail::Tx<H>;

inline auto unix_now() -> long long {
    return web::auth::detail::now();
}

inline auto random_bytes(std::size_t n) -> std::string {
    web::crypto::init();
    std::string out(n, '\0');
    randombytes_buf(out.data(), out.size());
    return out;
}

constexpr std::string_view k_register_new = "register_new";
constexpr std::string_view k_register_add = "register_add";
constexpr std::string_view k_login = "login";

struct Issued {
    std::string challenge_id;
    std::string challenge; // base64url
};

struct Consumed {
    std::string purpose;
    std::string challenge; // raw bytes
    std::string user_id;   // empty for login
};

template<typename H>
auto issue_challenge(H& h, std::string_view purpose, std::string_view user_id, long long now) -> Issued {
    h.exec("DELETE FROM auth_challenges WHERE expires_at <= ?", now);
    const auto id = random_bytes(16);
    std::string hex((id.size() * 2) + 1, '\0');
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): libsodium takes unsigned char*
    sodium_bin2hex(hex.data(), hex.size(), reinterpret_cast<const unsigned char*>(id.data()), id.size());
    hex.pop_back();
    const auto challenge = random_bytes(32);
    h.exec("INSERT INTO auth_challenges (id, purpose, challenge, user_id, expires_at) VALUES (?, ?, ?, ?, ?)", hex,
           std::string(purpose), webauthn::base64url_encode(challenge), std::string(user_id),
           now + config().challenge_ttl.count());
    return {.challenge_id = hex, .challenge = webauthn::base64url_encode(challenge)};
}

// Deletes the row as it reads it, so a challenge can be answered once.
template<typename H>
auto consume_challenge(H& h, std::string_view id, long long now) -> std::optional<Consumed> {
    const auto rows = h.template query_as<std::string, std::string, std::string>(
        "DELETE FROM auth_challenges WHERE id = ? AND expires_at > ? RETURNING purpose, challenge, user_id",
        std::string(id), now);
    if (rows.empty()) {
        return std::nullopt;
    }
    const auto& [purpose, challenge, user_id] = rows.front();
    auto raw = webauthn::base64url_decode(challenge);
    if (!raw) {
        return std::nullopt;
    }
    return Consumed{purpose, std::move(*raw), user_id};
}

inline auto fail(Failure::Kind k, std::optional<webauthn::Error> d = std::nullopt) -> std::unexpected<Failure> {
    return std::unexpected(Failure{.kind = k, .detail = d});
}

} // namespace detail

// Idempotent. Call once at startup, after users::create_schema.
template<typename H>
void create_schema(H& h) {
    h.exec("CREATE TABLE IF NOT EXISTS auth_passkeys ("
           " credential_id TEXT PRIMARY KEY,"
           " user_id TEXT NOT NULL REFERENCES auth_users(id) ON DELETE CASCADE,"
           " public_key TEXT NOT NULL,"
           " sign_count BIGINT NOT NULL,"
           " backup_eligible SMALLINT NOT NULL,"
           " backup_state SMALLINT NOT NULL,"
           " label TEXT NOT NULL,"
           " created_at BIGINT NOT NULL,"
           " last_used_at BIGINT NOT NULL)");
    h.exec("CREATE INDEX IF NOT EXISTS auth_passkeys_user ON auth_passkeys (user_id)");
    h.exec("CREATE TABLE IF NOT EXISTS auth_challenges ("
           " id TEXT PRIMARY KEY,"
           " purpose TEXT NOT NULL,"
           " challenge TEXT NOT NULL,"
           " user_id TEXT NOT NULL,"
           " expires_at BIGINT NOT NULL)");
    h.exec("CREATE INDEX IF NOT EXISTS auth_challenges_expires ON auth_challenges (expires_at)");
}

// ── registration ─────────────────────────────────────────────────────────────

// Start adding a passkey. With `user_id`, the passkey joins that signed-in user; without, finishing
// creates a new user. `user_name` is what the OS shows next to the passkey.
template<typename H>
auto begin_registration(H& h, const std::optional<std::string>& user_id, std::string_view user_name,
                        long long now = detail::unix_now()) -> std::expected<Options, Failure> {
    if (user_id && !web::auth::user_exists(h, *user_id)) {
        return detail::fail(Failure::Kind::no_such_user);
    }
    const auto& cfg = config();
    std::string handle_id;
    if (user_id) {
        handle_id = *user_id;
    } else {
        handle_id = web::auth::detail::new_user_id();
    }
    const auto issued =
        detail::issue_challenge(h, user_id ? detail::k_register_add : detail::k_register_new, handle_id, now);

    revet::json::Writer w;
    w.object();
    w.key("rp").object().key("id").value(cfg.rp.rp_id).key("name").value(cfg.rp_name).end();
    w.key("user")
        .object()
        .key("id")
        .value(webauthn::base64url_encode(handle_id))
        .key("name")
        .value(user_name)
        .key("displayName")
        .value(user_name)
        .end();
    w.key("challenge").value(issued.challenge);
    w.key("pubKeyCredParams").array().object().key("type").value("public-key").key("alg").value(-7).end().end();
    w.key("timeout").value(static_cast<long long>(cfg.challenge_ttl.count()) * 1000);
    w.key("attestation").value("none");
    w.key("authenticatorSelection")
        .object()
        .key("residentKey")
        .value("required")
        .key("requireResidentKey")
        .value(true)
        .key("userVerification")
        .value(cfg.rp.require_user_verification ? "required" : "preferred")
        .end();
    w.key("excludeCredentials").array();
    if (user_id) {
        for (const auto& p :
             h.template query_as<std::string>("SELECT credential_id FROM auth_passkeys WHERE user_id = ?", *user_id)) {
            w.object().key("type").value("public-key").key("id").value(std::get<0>(p)).end();
        }
    }
    w.end(); // excludeCredentials
    w.end(); // root
    return Options{issued.challenge_id, std::move(w).str()};
}

namespace detail {

// A real ceremony sends a few hundred bytes (a long attestation certificate chain, a few KiB). Without a cap a
// request could carry tens of MiB of CBOR, which decodes into gigabytes of nodes. Checked on the encoded
// text, before anything is decoded.
constexpr std::size_t k_max_client_data = 4096;
constexpr std::size_t k_max_attestation = 32768;
constexpr std::size_t k_max_authenticator_data = 4096;
constexpr std::size_t k_max_signature = 1024;

inline auto too_big(std::string_view base64url, std::size_t max_decoded) -> bool {
    return base64url.size() > ((max_decoded * 4) / 3) + 4;
}

} // namespace detail

// The three fields are what the app got back from the OS, base64url encoded. Pass `expect` whenever
// the route knows who is calling (see Expect); the default accepts any challenge.
template<typename H>
auto finish_registration(H& h, std::string_view challenge_id, std::string_view client_data_json_b64,
                         std::string_view attestation_object_b64, std::string_view label,
                         long long now = detail::unix_now(),
                         const Expect& expect = Expect::any()) -> std::expected<Registered, Failure> {
    if (detail::too_big(client_data_json_b64, detail::k_max_client_data) ||
        detail::too_big(attestation_object_b64, detail::k_max_attestation)) {
        return detail::fail(Failure::Kind::bad_request);
    }
    const auto cdj = webauthn::base64url_decode(client_data_json_b64);
    const auto att = webauthn::base64url_decode(attestation_object_b64);
    if (!cdj || !att || cdj->empty() || att->empty()) {
        return detail::fail(Failure::Kind::bad_request);
    }

    detail::Tx<H> tx(h);
    const auto ch = detail::consume_challenge(h, challenge_id, now);
    if (!ch || (ch->purpose != detail::k_register_new && ch->purpose != detail::k_register_add)) {
        tx.commit(); // the consume is kept: a challenge that was tried is spent
        return detail::fail(Failure::Kind::challenge_invalid);
    }
    const bool is_new = ch->purpose == detail::k_register_new;
    if (!expect.permits(is_new, ch->user_id)) {
        tx.commit();
        return detail::fail(Failure::Kind::user_mismatch);
    }

    const auto verified = webauthn::verify_registration(config().rp, ch->challenge, *cdj, *att);
    if (!verified) {
        tx.commit();
        return detail::fail(Failure::Kind::verification_failed, verified.error());
    }

    const auto credential_id = webauthn::base64url_encode(verified->credential_id);
    if (web::auth::find_user(h, "passkey", credential_id)) {
        tx.commit();
        return detail::fail(Failure::Kind::credential_taken);
    }
    if (is_new) {
        if (!web::auth::create_user_within(h, ch->user_id, "passkey", credential_id)) {
            return detail::fail(
                Failure::Kind::credential_taken); // rolls back, challenge included: retry with a new one
        }
    } else {
        const auto linked = web::auth::link_identity_within(h, ch->user_id, "passkey", credential_id);
        if (linked == web::auth::LinkResult::no_such_user) {
            return detail::fail(Failure::Kind::no_such_user);
        }
        if (linked != web::auth::LinkResult::linked) {
            return detail::fail(Failure::Kind::credential_taken);
        }
    }
    h.exec("INSERT INTO auth_passkeys (credential_id, user_id, public_key, sign_count, backup_eligible, backup_state, "
           "label,"
           " created_at, last_used_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
           credential_id, ch->user_id, webauthn::base64url_encode(verified->public_key),
           static_cast<long long>(verified->sign_count), verified->backup_eligible ? 1 : 0,
           verified->backup_state ? 1 : 0, std::string(label), now, 0LL);
    tx.commit();
    return Registered{ch->user_id, credential_id, is_new};
}

// finish_registration at the current time, for a caller that knows who it is: the usual call.
template<typename H>
auto finish_registration(H& h, std::string_view challenge_id, std::string_view client_data_json_b64,
                         std::string_view attestation_object_b64, std::string_view label,
                         const Expect& expect) -> std::expected<Registered, Failure> {
    return finish_registration(h, challenge_id, client_data_json_b64, attestation_object_b64, label, detail::unix_now(),
                               expect);
}

// ── sign in ──────────────────────────────────────────────────────────────────

// No username: the OS offers the passkeys it holds for this site.
template<typename H>
auto begin_login(H& h, long long now = detail::unix_now()) -> Options {
    const auto& cfg = config();
    const auto issued = detail::issue_challenge(h, detail::k_login, "", now);
    revet::json::Writer w;
    w.object();
    w.key("challenge").value(issued.challenge);
    w.key("rpId").value(cfg.rp.rp_id);
    w.key("timeout").value(static_cast<long long>(cfg.challenge_ttl.count()) * 1000);
    w.key("userVerification").value(cfg.rp.require_user_verification ? "required" : "preferred");
    w.end();
    return {issued.challenge_id, std::move(w).str()};
}

// `user_handle_b64` is optional; when the OS supplies it, it must match the passkey's owner.
template<typename H>
auto finish_login(H& h, std::string_view challenge_id, std::string_view credential_id_b64,
                  std::string_view client_data_json_b64, std::string_view authenticator_data_b64,
                  std::string_view signature_b64, std::string_view user_handle_b64 = {},
                  long long now = detail::unix_now()) -> std::expected<LoggedIn, Failure> {
    if (detail::too_big(client_data_json_b64, detail::k_max_client_data) ||
        detail::too_big(authenticator_data_b64, detail::k_max_authenticator_data) ||
        detail::too_big(signature_b64, detail::k_max_signature) || user_handle_b64.size() > 256) {
        return detail::fail(Failure::Kind::bad_request);
    }
    const auto cdj = webauthn::base64url_decode(client_data_json_b64);
    const auto ad = webauthn::base64url_decode(authenticator_data_b64);
    const auto sig = webauthn::base64url_decode(signature_b64);
    if (!cdj || !ad || !sig || cdj->empty() || ad->empty() || sig->empty() || credential_id_b64.empty() ||
        credential_id_b64.size() > 1400) {
        return detail::fail(Failure::Kind::bad_request);
    }
    std::optional<std::string> handle;
    if (!user_handle_b64.empty()) {
        handle = webauthn::base64url_decode(user_handle_b64);
        if (!handle) {
            return detail::fail(Failure::Kind::bad_request);
        }
    }

    detail::Tx<H> tx(h);
    const auto ch = detail::consume_challenge(h, challenge_id, now);
    if (!ch || ch->purpose != detail::k_login) {
        tx.commit();
        return detail::fail(Failure::Kind::challenge_invalid);
    }

    const auto rows = h.template query_as<std::string, std::string, long long>(
        "SELECT user_id, public_key, sign_count FROM auth_passkeys WHERE credential_id = ?",
        std::string(credential_id_b64));
    if (rows.empty()) {
        tx.commit();
        return detail::fail(Failure::Kind::unknown_credential);
    }
    const auto& [user_id, public_key_b64, stored_count] = rows.front();
    if (handle && *handle != user_id) {
        tx.commit();
        return detail::fail(Failure::Kind::user_mismatch);
    }
    const auto public_key = webauthn::base64url_decode(public_key_b64);
    if (!public_key) {
        throw std::runtime_error("auth: stored passkey public key is corrupt");
    }

    const auto verified = webauthn::verify_assertion(config().rp, ch->challenge, *public_key,
                                                     static_cast<std::uint32_t>(stored_count), *cdj, *ad, *sig);
    if (!verified) {
        tx.commit();
        return detail::fail(Failure::Kind::verification_failed, verified.error());
    }

    // Compare-and-swap on the counter so two logins racing on one credential cannot both succeed.
    const auto won =
        h.template scalar<std::string>("UPDATE auth_passkeys SET sign_count = ?, backup_state = ?, last_used_at = ?"
                                       " WHERE credential_id = ? AND sign_count = ? RETURNING credential_id",
                                       static_cast<long long>(verified->sign_count), verified->backup_state ? 1 : 0,
                                       now, std::string(credential_id_b64), stored_count);
    if (!won) {
        tx.commit();
        return detail::fail(Failure::Kind::verification_failed, webauthn::Error::sign_count_regressed);
    }
    tx.commit();
    return LoggedIn{user_id, std::string(credential_id_b64)};
}

// ── managing a user's passkeys ───────────────────────────────────────────────

template<typename H>
[[nodiscard]] auto list_passkeys(H& h, std::string_view user_id) -> std::vector<PasskeyInfo> {
    std::vector<PasskeyInfo> out;
    for (auto& [id, label, created, used, be, bs] :
         h.template query_as<std::string, std::string, long long, long long, int, int>(
             "SELECT credential_id, label, created_at, last_used_at, backup_eligible, backup_state FROM auth_passkeys"
             " WHERE user_id = ? ORDER BY created_at, credential_id",
             std::string(user_id))) {
        out.push_back({std::move(id), std::move(label), created, used, be != 0, bs != 0});
    }
    return out;
}

// Remove one passkey. The user's last way to sign in (passkey or any other identity) stays.
template<typename H>
auto remove_passkey(H& h, std::string_view user_id, std::string_view credential_id) -> RemoveResult {
    detail::Tx<H> tx(h);
    const auto owner = h.template scalar<std::string>("SELECT user_id FROM auth_passkeys WHERE credential_id = ?",
                                                      std::string(credential_id));
    if (!owner || *owner != user_id) {
        return RemoveResult::not_found;
    }
    // not_found here would mean the identity row is missing; the passkey row is removed regardless.
    if (web::auth::unlink_identity_within(h, user_id, "passkey", credential_id) ==
        web::auth::UnlinkResult::last_identity) {
        return RemoveResult::last_login_method;
    }
    h.exec("DELETE FROM auth_passkeys WHERE credential_id = ?", std::string(credential_id));
    tx.commit();
    return RemoveResult::removed;
}

} // namespace web::auth::passkeys
