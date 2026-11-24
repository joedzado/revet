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

**Table.** `auth_sessions(id, user_id, token_hash, generation, label, created_at, last_used_at, expires_at, revoked_at)`; `user_id` cascades from `auth_users`, so `delete_user` removes a user's sessions. A refresh token reads `<session id>.<generation>.<secret>.<mac>`. The MAC (keyed BLAKE2b under a key derived from `REVET_COOKIE_KEY`) means only a token this server issued can reach the session, so a forged old-generation token cannot trigger the reuse revocation.

**Key.** The access token uses the same key as cookies. After rotating `REVET_COOKIE_KEY`, existing access tokens fail, and apps get a new one on their next refresh. Refresh tokens carry a MAC under it too, so rotating the key signs every device out.

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
- **`web::cbor`.** `decode(bytes)` / `decode_prefix(bytes)` handle the CBOR subset WebAuthn uses and refuse the rest (tags, floats, indefinite lengths, duplicate map keys, more than 16 levels of nesting, invalid UTF-8, arrays or maps over 256 entries, counts larger than the input) without allocating or recursing unboundedly.
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

- **Recovery** is magic links (next section): a user who added an email address gets back in with a mailed link, then registers a new passkey. A user who never added one and loses every passkey is locked out, so prompt people to add an address.
- **Rate limiting.** The public endpoints (`register_*`, `login_*`) accept anonymous requests. Put `web::rate_limit::by_ip` in their `api_guards()` before exposing them.

## Session endpoints (`<revet/auth/session_http.hpp>`)

What keeps a native app signed in and lets the user leave. Same shape as the passkey handlers: each takes the request and a callable that opens a write handle.

| Route | Handler | Body | Returns |
|---|---|---|---|
| `POST /api/v1/auth/refresh` | `refresh` | `{refresh_token}` | `{tokens}` |
| `POST /api/v1/auth/logout` | `logout` | | `{ok: true}`; ends this device's session |
| `POST /api/v1/auth/logout-all` | `logout_all` | | `{ok: true, signed_out: n}` |
| `GET /api/v1/auth/devices` | `devices` | | `{devices: [{session_id, label, created_at, last_used_at, current}]}` |
| `POST /api/v1/auth/account/delete` | `delete_account` | | `{ok: true}`; removes the user, identities, sessions and passkeys |

`refresh` is public; the rest sit behind `tokens::guard()`. A refused refresh is always 401 with one of `invalid_refresh_token`, `session_expired`, `session_revoked`; the app should send the user to sign in again, and treat `session_revoked` as possible token theft. Send each refresh once and save the reply before using it (see the note on retries under Tokens). Account deletion is final and is what the App Store and Play Store require an app to offer.

## Rate limits (`<revet/auth/limits.hpp>`, `<revet/rate_limit.hpp>`)

Anything public that checks a secret or creates rows needs a ceiling. `web::auth::limits` gives two ready-made guard lists:

```cpp
static auto api_guards() -> std::vector<web::Middleware> { return web::auth::limits::anonymous(); }  // public routes
static auto api_guards() -> std::vector<web::Middleware> { return web::auth::limits::signed_in(); }  // token guard, then per user
```

| Limit | Applies to | Default |
|---|---|---|
| per client address | every anonymous route, one shared bucket | 20 requests, then 1 per 3 s |
| per credential or session | the `credential_id` or `refresh_token` named in the JSON body, across addresses | 10 requests, then 1 per 30 s |
| per user | signed-in routes | 60 requests, then 1 per second |

Refused requests get `429` with `Retry-After`. Change the numbers with `web::auth::limits::config()` at startup. Limits live in process memory, so they reset on restart and are not shared between processes. Behind a reverse proxy set `REVET_TRUSTED_PROXIES` (see [api.md](api.md#rate-limiting-revetrate_limithpp-namespace-webrate_limit)) or every visitor counts as the proxy.

## Browser pages (template)

The template ships a passkey sign-in page and a passkey management page, for web users and as a working example.

- **`/login`**: "Sign in with a passkey", and "Create an account with a passkey" with a name field. Replaces the demo username/password form.
- **`/account/passkeys`**: lists the user's passkeys, with Add and Remove. Redirects to `/login` when signed out. Add and Remove swap the list in place with htmx.
- **`app/scripts/passkey.js`**: about 70 lines, no libraries; it hands the server's options to `navigator.credentials` through the browser's own `parse…FromJSON` and `toJSON` helpers and posts the results. Needs Chrome 129, Safari 18.4 or Firefox 135; older browsers get "This browser does not support passkeys."
- **Cookie routes** under `/passkey/` (`login/options`, `login/finish`, `register/options`, `register/finish`) mirror the JSON API but start a cookie session instead of returning tokens. They are ordinary routes, so the global CSRF check applies; the page passes its token in `data-csrf`.
- The cookie holds the user id in `Session.username`. Read it with `web::State<Session>`.

A browser and a native app can share one account: a passkey registered in either works in both, provided the native app's origin is listed (see Setting up the native apps).

## Magic links (`<revet/auth/magic.hpp>`, `<revet/auth/magic_http.hpp>`, `<revet/mail.hpp>`)

Sign in by email, with no password. The user types an address, gets a link, opens it, and is in; the first time, that also creates the account. It is the recovery path for passkeys (a user who lost every passkey asks for a link to their address), and it works for people on a device with no passkey at all.

```cpp
magic::config().base_url = "https://app.example.com";   // or REVET_BASE_URL (required, https)
magic::create_schema(db);                               // after users::create_schema

magic::request(db, "ann@example.com");                  // queues the mail
auto done = magic::complete(db, token);                 // Completed{user_id, new_user, linked} or invalid_link
```

**Configuration** (environment): `REVET_BASE_URL` (the site's own address; links are built from this, never from the request's Host header), `REVET_APP_NAME` (in the mail), `REVET_MAGIC_TTL_SECONDS` (default 900), and the mail server: `REVET_SMTP_URL` (`smtps://host:465` or `smtp://host:587` with STARTTLS), `REVET_SMTP_USER`, `REVET_SMTP_PASS`, `REVET_MAIL_FROM` (`"My App <login@example.com>"`), optionally `REVET_SMTP_CA_FILE`.

**JSON endpoints** (for native apps), in `magic_http.hpp`:

| Route | Handler | Body | Returns |
|---|---|---|---|
| `POST /api/v1/auth/magic/request` | `request` | `{email}` | `{"ok":true}`; with an access token, the link adds the address to that account |
| `POST /api/v1/auth/magic/verify` | `verify` | `{token, device?}` | `{user_id, new_user, tokens}`, or `{user_id, linked: true}` for an add-address link |

The mail's link is `https://app.example.com/auth/magic?token=...`. Register that path as a universal link (iOS) and app link (Android): the Apple and Android association files this framework serves already list it (`app_link_paths`). The app takes `token` from the link and POSTs it to `verify`; on a device without the app the link opens the web page.

**Browser pages** (template): the sign-in page has an "Email me a link" form; `/auth/magic` shows "Sign in as ann@example.com" with a button; the button posts to `/auth/magic/confirm`, which starts the cookie session. The account page lists email addresses and adds more.

**How it is kept safe**
- The token is 256 random bits; only its SHA-256 is stored. It works once and expires (15 minutes). Spending is a single atomic update, so two clicks cannot both win.
- Opening the link only shows a page. The button's POST is what spends the token, so a mail scanner that fetches every link cannot use one up. The page sends `Referrer-Policy: no-referrer` and `Cache-Control: no-store`.
- `request` answers the same for every valid address, whether or not it has an account and whether or not a mail was sent. A malformed address is a 400.
- Mail is limited per client address, per recipient (3 in a burst, then one per 20 minutes), for the app as a whole (60 in a burst, then 30 a minute; `limits::magic_request()`), and per address per hour in the database (5, so it holds across restarts and processes).
- An address is validated to a strict ASCII form before anything is built; nothing a user types can add a header or an SMTP command. Bodies are base64 so nothing in them is read as protocol.
- Mail goes out from a background thread with retries (4 tries, pausing 2 s, 8 s, 32 s), so a slow mail server never holds up a request. Failures are logged with the recipient's domain only.
- Plain `smtp://` without TLS is refused unless the server is on this machine or `REVET_SMTP_ALLOW_PLAINTEXT=1`. Certificates are verified. `REVET_SMTP_CA_FILE` (a private relay's CA) works where libcurl uses OpenSSL (Linux); on macOS libcurl uses SecureTransport and trusts only the system keychain, so install the CA there.
- A signed-in user adding an address needs to click the link too, so nobody can add an address they cannot read. Adding an address never hands out a session.
- In development with no mail server and a `http://localhost` base URL, the template prints the link to the log instead of sending. It never does that otherwise: a link in a log is a login for anyone who can read the log.

**Before you launch:** set up SPF, DKIM and DMARC for the sender's domain (or use a mail provider that does), or sign-in mail lands in spam. Send a test to Gmail and iCloud. Check the sender address can receive replies or bounces.

## Putting it together

1. In `site_init.cpp`: `web::auth::create_schema`, `tokens::create_schema`, `passkeys::create_schema` (the template does this), and set the passkey config.
2. Set the environment: `REVET_COOKIE_KEY` (64 hex), `REVET_RP_ID`, `REVET_WEBAUTHN_ORIGINS`, `REVET_TRUSTED_PROXIES` when behind a proxy, `REVET_FORCE_SECURE_COOKIES=1`.
3. Keep the routes under `app/routes/api/v1/auth/` with their guards. Add `api_guards()` with `tokens::guard()` (or `limits::signed_in()`) to your own `/api` routes, and read the caller with `web::api::principal<web::auth::tokens::Principal>()`.
4. Run `tokens::purge_ended` now and then (a startup call or a daily job) to forget finished sessions.
5. Work through the [security checklist](security.md).
