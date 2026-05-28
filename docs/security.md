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
- [ ] **`THIRD_PARTY_LICENSES` ships with the binary.** Required by libcurl, simdjson, CTRE, libsodium, libpq when redistributing. See [deploy.md → Third-party license attribution](deploy.md#third-party-license-attribution).
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
| CSRF | yes | AEAD-sealed cookie + raw token in form/header. Defeats subdomain cookie injection. |
| XSS via HTML DSL | yes | All text rendered via `html::*` is HTML-escaped by default. Use `html::raw(...)` only for trusted strings. |
| SQL injection | yes | Generated queries (`gen_queries.py`) emit parameterized SQL exclusively. `db::sqlite::Handle::query/exec` only takes bound params. |
| Timing oracles on token compare | yes | `web::crypto::secure_eq` uses `sodium_memcmp`. CSRF + `bearer_static` use it. Custom auth must too. |
| Cookie tampering | yes | State cookies and CSRF cookie are AEAD-sealed; tampering fails verification. |
| Cookie theft via plain HTTP | opt-in | Enable via `secure_cookies(true)`. Off by default for dev. |
| Rate limiting / brute force | no | Not in scope — put it in your reverse proxy or middleware. |
| Replay attacks on bearer tokens | no | Tokens are not time-bound or single-use unless the verifier checks revocation. |
| Subresource integrity (SRI) | no | Bundled htmx is inlined into the binary; you serve it from `/_revet/`. External CDNs need SRI hashes from you. |
| Audit logging | no | Application concern — no built-in audit log. |
| Secret scanning in logs | no | The logger does not redact. Don't log `req.headers["authorization"]` or `req.cookies`. |

## Reporting a vulnerability

revet is pre-release and has no formal security disclosure process yet. If you find something serious, contact the maintainer directly rather than filing a public issue. A `SECURITY.md` with a real reporting channel will land before 1.0.
