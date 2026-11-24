# Security

## Pre-deploy checklist

Before pointing real traffic at a revet app, verify every item below.

- [ ] **`REVET_COOKIE_KEY` is set** to a 64-char hex string (32 bytes of entropy). Without it the framework falls back to a key derived from `hostname()` — fine for dev, catastrophic in prod (any process on the same host can forge cookies).
- [ ] **`web::config::secure_cookies(true)`** is called at startup *or* `REVET_FORCE_SECURE_COOKIES=1` is in the env. Off by default for localhost dev; the `Secure` attribute is required when serving over HTTPS.
- [ ] **CSRF middleware is registered** (`web::use(web::csrf::middleware)`) for any app that mutates server state. State-changing methods (POST/PUT/PATCH/DELETE) without a valid token return 403.
- [ ] **API endpoints have `api_guards()`** returning at least one auth guard. Without it `/api/*` routes are public.
- [ ] **`bearer_static` (or `bearer` with `secure_eq`)** is used for token compares. Never `tok == "secret"` — that's a timing oracle.
- [ ] **TLS terminates upstream** of the revet process (nginx, ALB, Cloudflare). revet does not speak TLS itself.
- [ ] **File-upload buckets** in `revet.toml [uploads.*]` point at dirs the framework can chmod 0700/0750. Default scaffolding uses `uploads/files/{incoming,validated}` — confirm they are NOT inside any publicly-served static dir.
- [ ] **`-Werror`** is on for production builds (`cmake -DREVET_STRICT=ON`). Catches the warnings that pile up in dev.
- [ ] **Database file (SQLite)** is not in a directory served as static assets. `revet.toml` `path = "build/data/users.db"` — confirm `build/data/` is excluded from any reverse-proxy static fall-through.
- [ ] **Error pages** don't leak internals. `web::on_error(status, handler)` lets you replace the default 500 body. Check no stack traces / SQL fragments leak.
- [ ] **`THIRD_PARTY_LICENSES` ships with the binary.** Required by libcurl, simdjson, CTRE, libsodium, OpenSSL, libpq when redistributing. See [deploy.md → Third-party license attribution](deploy.md#third-party-license-attribution).
- [ ] **`REVET_TRUSTED_PROXIES` lists your reverse proxy** (e.g. `127.0.0.1`) when using `web::rate_limit`. If it is unset, every client behind the proxy shares the proxy's address and one bucket; if it is too broad, clients can forge `X-Forwarded-For` and dodge limits.
- [ ] **Passkey login is configured for the real domain**: `REVET_RP_ID` (the bare domain, chosen once: changing it orphans every passkey), `REVET_WEBAUTHN_ORIGINS` (exact `https://` origins, plus `android:apk-key-hash:` for the Android app), `REVET_RP_NAME`. The template falls back to `localhost` and logs a warning; that only works in development. See [auth.md](auth.md#passkeys-revetauthpasskeyshpp-revetauthpasskeys_httphpp-namespace-webauthpasskeys).
- [ ] **`/.well-known/apple-app-site-association` and `/.well-known/assetlinks.json` are reachable over HTTPS** with no redirect, and the proxy passes `/.well-known/` through to the app. Set `apple_app_ids` and `android_apps` in `site_init.cpp`.
- [ ] **Every public auth route has `web::auth::limits::anonymous()`** in its `api_guards()` / `route_guards()` and every signed-in one `signed_in()`. The template does this; new routes copied from elsewhere may not.
- [ ] **Users have a second way to sign in.** Prompt people to add an email address (the account page does): it is the recovery path if every passkey is lost.
- [ ] **Magic-link mail is configured and deliverable**: `REVET_BASE_URL` (https), `REVET_SMTP_URL` with TLS, `REVET_SMTP_USER`/`REVET_SMTP_PASS`, `REVET_MAIL_FROM`; SPF, DKIM and DMARC set up for the sender domain; a real test message reached Gmail and iCloud inboxes.
- [ ] **`/auth/magic*` routes keep their guards** (`limits::magic_request()` on the request route, `limits::anonymous()` on confirm).
- [ ] **Reverse proxy strips client-supplied `X-Forwarded-*` headers** and re-sets them itself. Otherwise a client can forge `X-Forwarded-For` and your logs/rate-limiter believe a fake IP.

## Generating `REVET_COOKIE_KEY`

```bash
openssl rand -hex 32
# or
python3 -c 'import secrets; print(secrets.token_hex(32))'
# or
hexdump -n 32 -v -e '/1 "%02x"' /dev/urandom
```

Store the key in your secret manager (1Password, Vault, AWS Secrets Manager, etc.), inject as env var at process start. Never commit it.

## Key rotation

The cookie key encrypts state cookies (`web::State<T>`) and the CSRF cookie via AEAD (XChaCha20-Poly1305). Consequences of rotation:

- **All active sessions and CSRF tokens are invalidated.** Existing cookies fail AEAD verification (AEAD = authenticated encryption; tampering or wrong key both fail closed) → user is silently re-issued a new one on the next GET.
- **No graceful overlap** today. There is no "previous key, current key" rolling window — if you need zero-downtime rotation, that needs to be added at the framework level. File it as an issue.
- **Plan it like a logout-all-users event.** Pick a low-traffic window, redeploy with the new key.

Recommend rotating: on suspected key compromise, on employee departure if the key was ever readable to humans, annually as hygiene.

## What revet protects against (and what it doesn't)

| Class | Status | Notes |
|---|---|---|
| CSRF | yes | AEAD-sealed cookie + raw token in form/header. Defeats subdomain cookie injection. `/api/*` is exempt: authenticate it with a header token, never a cookie. |
| XSS via HTML DSL | yes | All text rendered via `html::*` is HTML-escaped by default. Use `html::raw(...)` only for trusted strings. |
| SQL injection | yes | Generated queries (`gen_queries.py`) emit parameterized SQL exclusively. `db::sqlite::Handle::query/exec` only takes bound params. |
| Timing oracles on token compare | yes | `web::crypto::secure_eq` uses `sodium_memcmp`. CSRF + `bearer_static` use it. Custom auth must too. |
| Cookie tampering | yes | State cookies and CSRF cookie are AEAD-sealed; tampering fails verification. |
| Cookie theft via plain HTTP | opt-in | Enable via `secure_cookies(true)`. Off by default for dev. |
| Rate limiting / brute force | opt-in | `web::rate_limit::by_ip` / `by_key` middleware; per process, in memory. See [api.md](api.md#rate-limiting-revetrate_limithpp-namespace-webrate_limit). |
| Replay attacks on bearer tokens | partial | `bearer`/`bearer_static` tokens are not time-bound. `web::auth::tokens` access tokens expire (15 min); refresh tokens are single use and MACed, and presenting an old one revokes the session. |
| Phishing, credential stuffing, password reuse | yes, with passkeys | A passkey is bound to the site's domain by the browser/OS and holds no shared secret. The server stores public keys only. |
| Replayed or forged passkey answers | yes | Challenges are random, single use, expire in 5 minutes, and are tied to one ceremony and one caller: a signed-in user can finish only a challenge issued to them, and a signed-out caller only a new-account one. Origin, RP ID, signature and signature counter are verified. |
| Stolen refresh token | partial | Rotation plus reuse detection ends the session when both thief and owner use it. Revocation takes effect at the next refresh, or at once with `tokens::guard_checked`. |
| Account takeover by guessing | yes | `web::auth::limits` throttles per client address and per credential or session, and per user once signed in. Limits are per process, in memory. |
| Account recovery | partial | A user with a verified email address gets back in with a magic link. A user who never added one and loses every passkey is locked out. |
| Mail abuse (spamming an address, running up the SMTP bill) | yes | Per-client, per-recipient and global limits on "mail me a link", plus a per-address cap in the database. Responses do not reveal whether an address has an account. |
| Link scanners using up one-time links | yes | The link opens a page; only the button's POST spends the token. |
| Forged Host header in a mailed link | yes | Links use `REVET_BASE_URL`, never the request's Host. |
| Subresource integrity (SRI) | no | Bundled htmx is inlined into the binary; you serve it from `/_revet/`. External CDNs need SRI hashes from you. |
| Audit logging | no | Application concern — no built-in audit log. |
| Secret scanning in logs | no | The logger does not redact. Don't log `req.headers["authorization"]` or `req.cookies`. |

## Reporting a vulnerability

revet is pre-release and has no formal security disclosure process yet. If you find something serious, contact the maintainer directly rather than filing a public issue. A `SECURITY.md` with a real reporting channel will land before 1.0.
