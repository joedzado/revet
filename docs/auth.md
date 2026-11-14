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
