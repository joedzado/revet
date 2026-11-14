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

## Passkeys (`<revet/auth/passkeys.hpp>`, `<revet/auth/passkeys_http.hpp>`, namespace `web::auth::passkeys`)

Passwordless login for native apps. The OS keeps the private key (iCloud Keychain, Google Password Manager, a security key) and unlocks it with Face ID, a fingerprint or a PIN. The server keeps the public key and checks a signature. There is nothing to leak, phish or reset.

```cpp
// site_init.cpp, once
auto& pk = web::auth::passkeys::config();
pk.rp.rp_id = "app.example.com";                      // or REVET_RP_ID
pk.rp.origins = {"https://app.example.com",           // or REVET_WEBAUTHN_ORIGINS (comma-separated)
                 "android:apk-key-hash:<hash>"};
pk.rp_name = "Example App";                                   // or REVET_RP_NAME
pk.apple_app_ids = {"TEAMID.com.example.app"};
pk.android_apps = {{.package_name = "com.example.app", .sha256_cert_fingerprints = {"AA:BB:..."}}};

web::auth::create_schema(db);                          // users
web::auth::tokens::create_schema(db);                  // sessions
web::auth::passkeys::create_schema(db);                // passkeys and challenges
```

### The flow

1. **Register.** The app calls `register_options`, hands the returned `options` to the OS (`ASAuthorizationPlatformPublicKeyCredentialProvider` on iOS, `CredentialManager.createCredential` on Android), then posts the answer to `register_finish`. Called without a token, this creates an account and returns a session. Called with `Authorization: Bearer <access token>`, it adds a passkey to that user.
2. **Sign in.** The app calls `login_options`, hands `options` to the OS, which shows the user's passkeys for this site (no username to type), then posts the answer to `login_finish`. The response carries a session (see Tokens).

### Endpoints

Routes live in the template under `app/routes/api/v1/auth/`; each is one line calling a handler in `web::auth::passkeys::http`. Bodies are JSON; binary fields are base64url without padding.

| Route | Handler | Body | Returns |
|---|---|---|---|
| `POST /api/v1/auth/passkey/register/options` | `register_options` | `{display_name?}` | `{challenge_id, options}` |
| `POST /api/v1/auth/passkey/register/finish` | `register_finish` | `{challenge_id, client_data_json, attestation_object, label?, device?}` | `{user_id, credential_id, new_user, tokens?}` |
| `POST /api/v1/auth/passkey/login/options` | `login_options` | `{}` | `{challenge_id, options}` |
| `POST /api/v1/auth/passkey/login/finish` | `login_finish` | `{challenge_id, credential_id, client_data_json, authenticator_data, signature, user_handle?, device?}` | `{user_id, tokens}` |
| `GET /api/v1/auth/passkeys` | `list` | | `{passkeys: [{credential_id, label, created_at, last_used_at, backup_eligible, backup_state}]}` |
| `POST /api/v1/auth/passkeys/remove` | `remove` | `{credential_id}` | `{removed: true}` |

`tokens` is `{access_token, refresh_token, access_expires_at, refresh_expires_at, session_id}`. `list` and `remove` sit behind `tokens::guard()`. Errors are `{"error": "<code>"}`: `bad_request`, `challenge_invalid` (start over), `credential_taken` (409), `verification_failed` with a `reason` (registration only), and `login_failed` (401) for every way a sign-in can be wrong, so the response does not say whether a credential exists. `remove` answers 409 `last_login_method` rather than lock the user out.

### Library functions

For use without the HTTP layer: `begin_registration`, `finish_registration`, `begin_login`, `finish_login`, `list_passkeys`, `remove_passkey`. They return `std::expected<..., Failure>` where `Failure::kind` is one of `bad_request`, `challenge_invalid`, `verification_failed` (with the `webauthn::Error` in `detail`), `unknown_credential`, `credential_taken`, `user_mismatch`, `no_such_user`.

### What is stored

- `auth_passkeys(credential_id, user_id, public_key, sign_count, backup_eligible, backup_state, label, created_at, last_used_at)`. `user_id` cascades from `auth_users`, so `delete_user` removes a user's passkeys.
- `auth_challenges(id, purpose, challenge, user_id, expires_at)`. A challenge lives 5 minutes (`challenge_ttl`), answers once, and only for the ceremony it was issued for (a login challenge cannot finish a registration). It is spent when the answer arrives, right or wrong. Expired rows are swept whenever a ceremony starts.
- Each passkey is also an identity (`provider = "passkey"`, subject = credential id), so a user can pair passkeys with other sign-in methods and `unlink_identity` / `remove_passkey` both keep at least one way in.
- The sign-in counter is updated with a compare-and-swap, so two logins racing on one credential cannot both succeed.

### Setting up the native apps

Both platforms check that your app and your domain vouch for each other.

- **iOS.** Add the Associated Domains entitlement `webcredentials:app.example.com`. Set `apple_app_ids` to `<Team ID>.<bundle id>`; the server then answers `GET /.well-known/apple-app-site-association` with `{"webcredentials":{"apps":[...]}}`. Apple fetches it over HTTPS with no redirect.
- **Android.** Set `android_apps` to the package name and the SHA-256 fingerprint of the signing certificate (debug and release keys differ; list both while testing). The server answers `GET /.well-known/assetlinks.json` with the `get_login_creds` relation. Add `android:apk-key-hash:<base64url of the same SHA-256>` to `origins`.
- **Both.** The domain must be served over HTTPS. Behind Caddy or nginx, pass `/.well-known/` through to the app. `rp_id` is the bare domain, not the URL.
- **Passkeys follow the domain.** Changing `rp_id` later orphans every registered passkey. Pick it once.

### Not covered here

- **Recovery.** Losing every passkey with no other login method locks the account out. The rules above keep users from deleting their last method, but a lost phone with iCloud Keychain off is still a lost account. Add a second method (another passkey on a second device, or Sign in with Apple/Google) before launch; the identity model already supports it.
- **Rate limiting.** The public endpoints (`register_*`, `login_*`) accept anonymous requests. Put `web::rate_limit::by_ip` in their `api_guards()` before exposing them.
