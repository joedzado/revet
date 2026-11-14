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
