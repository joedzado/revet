# Auth

Login for apps built on revet. This page grows one section per piece; each section says what ships today.

## Users and identities (`<revet/auth/users.hpp>`, namespace `web::auth`)

A **user** is an opaque 32-character random id. An **identity** is one way that user signs in: a provider name (`"passkey"`, `"google"`, `"apple"`, ...) plus the provider's stable subject. A user can hold several identities, and a given (provider, subject) belongs to exactly one user.

```cpp
auto db = resources::main_db<db::Write>();
web::auth::create_schema(db);                                        // idempotent, call at startup
const auto id = web::auth::find_or_create_user(db, "google", sub);   // sign-in: finds or creates
web::auth::link_identity(db, id, "apple", apple_sub);                // add another way in
```

| Function | Does |
|---|---|
| `create_schema(h)` | Creates `auth_users` and `auth_identities` if missing. |
| `find_user(h, provider, subject)` | The user id for an identity, or `nullopt`. |
| `find_or_create_user(h, provider, subject)` | Existing user, or a new user plus the identity. Two requests racing on the same new identity get the same user. |
| `link_identity(h, user, provider, subject)` | `linked`, `already_linked`, `taken` (owned by another user), or `no_such_user`. |
| `unlink_identity(h, user, provider, subject)` | `unlinked`, `not_found`, or `last_identity` (refused: a user keeps one way in). |
| `identities_of(h, user)` | All identities of a user, oldest first. |
| `user_exists(h, user)` | Whether the id is a user. |
| `delete_user(h, user)` | Removes the user and every row that references them. `false` if there was no such user. |

- Works on SQLite and Postgres with the same code. Functions that write need a `db::Write` handle and run in their own transaction, so call them on a handle with no transaction open.
- Tables are `auth_users(id, created_at)` and `auth_identities(user_id, provider, subject, created_at)`, with `(provider, subject)` as the primary key. Times are unix seconds. The `auth_` prefix keeps them clear of an app's own `users` table.
- Later auth tables (sessions, passkeys) reference `auth_users(id)` with `ON DELETE CASCADE`, so `delete_user` also removes them. That is the account-deletion path app stores ask for.
- Provider and subject are stored as given. Verify identities before calling these functions; they do not.
- The template's `site_init.cpp` calls `create_schema` at startup.

## Tokens (`<revet/auth/tokens.hpp>`, namespace `web::auth::tokens`)

How a native app stays signed in after a login method (passkey, Apple, Google) has vouched for the user. One **session** per device. Each session has two tokens:

- **Access token**: short-lived (15 minutes), sealed with `REVET_COOKIE_KEY`, carries the user id, session id and expiry. Checked with no database read. Sent as `Authorization: Bearer <token>`.
- **Refresh token**: long-lived (60 days from sign-in), random, shown to the client once. The server stores only its SHA-256. Each use trades it for a new pair.

```cpp
web::auth::tokens::create_schema(db);                         // once at startup, after users::create_schema

auto pair = web::auth::tokens::issue(db, user_id, "Alex's iPhone");    // after a login method succeeds
// send pair.access_token, pair.refresh_token, pair.access_expires_at to the app

auto next = web::auth::tokens::refresh(db, refresh_token);    // std::expected<TokenPair, RefreshError>

static auto api_guards() -> std::vector<web::Middleware> { return { web::auth::tokens::guard() }; }
// in a handler:
const auto& who = web::api::principal<web::auth::tokens::Principal>();   // who.user_id, who.session_id
```

| Function | Does |
|---|---|
| `issue(h, user, label)` | Starts a session; returns the first token pair. |
| `refresh(h, refresh_token)` | New pair, or `invalid`, `expired`, `revoked`, `reuse_detected`. |
| `verify_access(token)` | The `Principal` for a valid, unexpired access token; otherwise `nullopt`. No database. |
| `guard()` | Route guard: 401 unless the request has a valid access token. No database read. |
| `guard_checked(open)` | As `guard()`, and the session must still be active, so sign-out bites at once. `open` returns a read handle. |
| `session_active(h, session)` | Not revoked and not past `refresh_ttl`. |
| `revoke_session(h, user, session)` | Sign out one device. Scoped to the user. |
| `revoke_all(h, user)` | Sign out everywhere; returns how many sessions ended. |
| `list_devices(h, user)` | Active sessions, most recently used first. |
| `purge_ended(h, before)` | Deletes sessions that ended before a time. Run now and then. |

**Rotation and theft.** Every refresh replaces the refresh token, and the old one stops working. Presenting an old token again means two parties hold copies, so the session is revoked and both lose access. One cost: a client that retries a refresh after losing the response will trip this and have to sign in again. Have apps send each refresh once and store the reply before using it.

**Sign-out.** Revoking a session stops refresh immediately. An access token already issued keeps working until it expires, up to `access_ttl`, unless the route uses `guard_checked`. Pick `guard_checked` for routes where that gap matters (money, account changes).

**Lifetimes.** `access_ttl` and `refresh_ttl` default to 15 minutes and 60 days. Set `REVET_ACCESS_TTL_SECONDS` / `REVET_REFRESH_TTL_SECONDS`, or assign `web::auth::tokens::config().access_ttl` at startup. A session ends at `refresh_ttl` after sign-in; refreshing does not extend it.

**Table.** `auth_sessions(id, user_id, token_hash, generation, label, created_at, last_used_at, expires_at, revoked_at)`; `user_id` cascades from `auth_users`, so `delete_user` removes a user's sessions. A refresh token reads `<session id>.<generation>.<secret>`.

**Key.** The access token uses the same key as cookies. After rotating `REVET_COOKIE_KEY`, existing access tokens fail, and apps get a new one on their next refresh. Refresh tokens are hashed, not sealed, so they are unaffected.

## WebAuthn core (`<revet/webauthn.hpp>`, `<revet/cbor.hpp>`, namespace `web::webauthn`, `web::cbor`)

The checks behind passkeys: what a relying party must verify when an authenticator registers a credential or signs a login. Pure functions, with no database, no HTTP and no state; challenges and credential storage belong to the caller. The routes and tables that use these live in the passkeys section below.

```cpp
web::webauthn::Config cfg{
    .rp_id = "app.example.com",
    .origins = {"https://app.example.com",
                "android:apk-key-hash:<base64url SHA-256 of your app's signing certificate>"}};

// Registration. The app sends clientDataJSON and attestationObject; base64url-decode them first.
auto reg = web::webauthn::verify_registration(cfg, challenge, client_data_json, attestation_object);
// on success store reg->credential_id, reg->public_key, reg->sign_count

// Login. clientDataJSON, authenticatorData, signature (also decoded).
auto ok = web::webauthn::verify_assertion(cfg, challenge, stored_public_key, stored_sign_count,
                                          client_data_json, authenticator_data, signature);
// on success store ok->sign_count
```

Both return `std::expected<..., web::webauthn::Error>`; `error_name(e)` gives a stable string for logs.

| Check | Error |
|---|---|
| `clientDataJSON` is an object with `type`, `challenge`, `origin` | `bad_client_data` |
| `type` is `webauthn.create` (registration) or `webauthn.get` (login) | `wrong_type` |
| challenge equals the one you issued (constant time); yours must be at least 16 bytes | `wrong_challenge` |
| origin is exactly one of `cfg.origins`; `crossOrigin` is not true | `wrong_origin`, `cross_origin` |
| attestation object and authenticator data parse; nothing left over | `bad_attestation`, `bad_auth_data` |
| RP ID hash is the SHA-256 of `cfg.rp_id` | `wrong_rp_id` |
| user-present flag; user-verified flag unless `require_user_verification = false` | `user_not_present`, `user_not_verified` |
| backed-up flag only with backup-eligible | `bad_backup_flags` |
| credential id present and at most 1023 bytes (registration) | `bad_credential_data` |
| public key is ES256 on P-256 and a real point on the curve | `unsupported_algorithm`, `bad_public_key` |
| ECDSA signature over `authenticatorData ‖ SHA-256(clientDataJSON)` (login) | `bad_signature` |
| counter went up, unless it is 0 both times (login) | `sign_count_regressed` |

- **Algorithm.** ES256 only (ECDSA, P-256, SHA-256): what iPhones, Android phones and most security keys produce. RS256 (some Windows Hello setups) is not supported.
- **Attestation.** Any `fmt` is accepted and the statement is not validated, so a registration proves the credential belongs to this site, not which make of authenticator holds it. That is the right default for a consumer app.
- **Android.** Android apps report their origin as `android:apk-key-hash:<hash>`. List that exact string in `origins`.
- **Synced passkeys** (iCloud Keychain, Google Password Manager) report a counter of 0 every time. That passes; a counter that was non-zero and falls back to 0 does not.
- **base64url.** `web::webauthn::base64url_encode` / `base64url_decode` use the unpadded form WebAuthn uses; decoding is strict.
- **`web::cbor`.** `decode(bytes)` / `decode_prefix(bytes)` handle the CBOR subset WebAuthn uses and refuse the rest (tags, floats, indefinite lengths, duplicate map keys, more than 16 levels of nesting, invalid UTF-8, counts larger than the input) without allocating or recursing unboundedly.
- **OpenSSL.** Signature checking uses OpenSSL (a Conan dependency, built from source the first time). The OpenSSL headers are included only from `src/webauthn.cpp`.
