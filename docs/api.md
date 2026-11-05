# Runtime API reference

Quick lookup for framework runtime types and helpers that aren't expanded inline in the README.

## HTTP client (`<revet/http.hpp>`, `<revet/http_client.hpp>`)

### `http::Response`

```cpp
struct Response {
    int status_code = 0;                                   // 0 = transport failure
    std::string body;
    std::map<std::string, std::string> headers;

    bool ok() const;                                       // 200-299
    explicit operator bool() const;                        // same as ok()
};
```

A successful HTTP transaction with any status code returns a `Response`. A transport failure (DNS / connect / timeout / TLS) returns `Response{0, "Curl error: ...", {}}` — `status_code == 0` is reserved and cannot collide with a real HTTP code.

### `http::Error`

```cpp
class Error : public std::runtime_error {
public:
    Error(int status_code, std::string body);
    int         status_code;        // 0 = transport failure
    std::string body;
};
```

Thrown by generated API client functions (`apis.hpp`) on non-2xx responses or transport failures. Catchable as `http::Error&` or `std::runtime_error&`.

The low-level `http::get(url) / http::post(url) / ...` helpers never throw — they return `Response`. Only the codegen-generated wrappers throw.

### `http::Client` + implementations

```cpp
class Client { ... };                  // abstract
class CurlClient final : public Client { ... };
class MockClient  final : public Client {
    void stub(method, url, Response);
    void clear();
    const std::vector<Call>& history() const;
};
```

The default client is a `CurlClient` shared via `http::global::default_client()`. Replace it for tests:

```cpp
auto mock = std::make_shared<http::MockClient>();
mock->stub("GET", "https://api.example.com/x", Response{200, "{...}", {}});
http::global::set_client(mock);
```

Default `CurlClient` timeouts: `CONNECTTIMEOUT=10`, `TIMEOUT=30`. Not configurable today — file an issue if you need per-call overrides.

## Cookies + sessions

### `web::config::secure_cookies(bool)`

Global toggle for `Secure` attribute on all `Set-Cookie` headers. Off by default; turn on at startup for HTTPS deployments. Also honors `REVET_FORCE_SECURE_COOKIES=1`.

### `web::State<T>` (encrypted) and `web::states::pack_plain` (plaintext)

For most cases use the encrypted variant — `web::State<MySession>` injects `MySession` from the cookie. The codegen also emits `pack_plain<T>` / `unpack_plain<T>` if you want a cookie that isn't encrypted (you give up tamper-resistance — only do this for non-sensitive data).

### `web::Context::current()`

Thread-local request context. Lives only for the duration of one request; cleared by the dispatcher when the response is written. Holds:

```cpp
std::flat_map<std::type_index, std::any> states;   // State<T> / Flash<T> / CSRF token
UploadedFiles                            files;     // populated by uploads::allow()
```

Coroutines: `Context::current()` reflects whatever thread the coroutine is *currently* running on. If you suspend on a `co_await` and resume on an IO thread, the context is the request thread's context only on resumption back to a request thread — IO threads have no request context. Capture anything you need into the coroutine frame before the first `co_await`.

## Crypto (`<revet/crypto.hpp>`)

```cpp
namespace web::crypto {
    std::string encrypt(std::string_view plaintext);     // → base64(nonce||ciphertext+MAC)
    std::string decrypt(std::string_view sealed);        // "" on failure (bad key / tampered)
    bool        secure_eq(std::string_view a, std::string_view b);
}
```

`encrypt` / `decrypt` use XChaCha20-Poly1305 via libsodium with `REVET_COOKIE_KEY` (or a hostname-derived dev key).

`secure_eq` is constant-time string comparison via `sodium_memcmp`. **Use this for every token / MAC / secret comparison.** Plain `==` leaks length and prefix information through response timing.

## Auth (`<revet/web.hpp>` namespace `web::api`)

```cpp
Middleware bearer_static(std::string expected);
Middleware bearer(std::function<bool(std::string_view)> verify);
template<typename T> Middleware bearer(std::function<std::optional<T>(std::string_view)> resolve);
template<typename T> const T& principal();
```

- `bearer_static("key")` — fixed-token guard, constant-time compare. Empty `expected` fails closed.
- `bearer(verify)` — custom predicate. **Inside `verify`, use `secure_eq`, not `==`.**
- `bearer<T>(resolve)` — typed principal; the resolved `T` is stashed in the context, retrieved with `principal<T>()` in the handler.

## Logging (`<revet/log.hpp>`)

```cpp
namespace log {
    enum class Level { DEBUG, INFO, WARN, ERROR };
    void set_level(Level);
    Level get_level();
    void debug(string_view tag, format args...);
    void info (string_view tag, format args...);
    void warn (string_view tag, format args...);
    void error(string_view tag, format args...);
}
```

Output goes to stderr only. Read `LOG_LEVEL` env at static-init. The framework defines `log::tags::ROUTES`, `log::tags::DB`, `log::tags::HTTP`, etc., but your `log::TuTag` may be any string.

## Async helpers

### `web::Task<T>`

Eager-suspending coroutine type. `co_return T` produces a `Task<T>`. Exceptions propagate through `co_await`. Drive with `std::move(task).start([](T) { ... })` in test code.

### `web::run_on_io(callable)`

Returns an awaitable that runs `callable` on an IO thread and resumes the awaiting coroutine when done. Use for blocking calls (sync DB, sync libcurl) that you want to offload from request threads. The generated `async_query` / `async_exec` and `http::Awaitable` are both built on this.

### `http::Awaitable<Response>`

Internal — used by `http::get/post/...`. You typically don't construct one yourself.

## See also

- [codegen.md](./codegen.md) for what each script generates.
- [attributes.md](./attributes.md) for the markers the codegen reads.
- [security.md](security.md) for the pre-deploy checklist.
- [config.md](config.md) for env vars + `revet.toml` reference.

## Postgres (`<revet/db/postgres.hpp>` namespace `db::postgres`)

Tested against Postgres 16 by `tests/test_postgres_e2e.cpp`, which is skipped unless `REVET_TEST_PG_URL` points at a throwaway database.

```cpp
auto h = resources::my_db<db::Write>();

// ? is a placeholder, except inside literals, comments and dollar quoting.
// With arguments, write ?? for a literal ?, such as the jsonb operator.
// With no arguments, the SQL is sent verbatim.
auto rows = h.query("select name from accounts where id = ?::uuid", id);

// query() reads NULL as "". Use query_nullable() where that matters.
auto cells = h.query_nullable("select closed_at::text from accounts");
if (!cells[0][0]) { /* NULL */ }

// nullptr and an empty std::optional bind as NULL.
h.exec("update accounts set note = ? where id = ?::uuid", std::optional<std::string>{}, id);

// Rolls back on scope exit unless committed.
db::postgres::Transaction tx(h);
h.exec("insert into log (msg) values (?)", msg);
tx.commit();
```

A connection returned to the pool has any open transaction rolled back. A lost connection makes the current query throw and is reconnected for the next one, so callers decide whether to retry.

## Decimal (`<revet/decimal.hpp>` namespace `revet`)

Exact fixed-point arithmetic for money, up to 38 significant digits. Never use `double` for money. Read `numeric` columns as text and parse them.

```cpp
auto total = revet::Decimal::parse("0.1") + revet::Decimal::parse("0.2"); // "0.3"
total.to_string();                                    // same text Postgres prints
total.rescale(2);                                     // ties away from zero, like round()
total.rescale(2, revet::Rounding::HalfEven);
a.divide(b, 4);                                       // result scale is explicit
```

Scale follows Postgres: `+` and `-` take the larger scale, `*` adds them. A result that does not fit throws `DecimalError`.

## Rails encrypted columns (`<revet/rails_encryption.hpp>` namespace `rails`)

Reads and writes `ActiveRecord::Encryption` columns with the default scheme. Needs hardware AES, so the constructor throws on a Raspberry Pi 4 or older.

```cpp
const rails::Encryption enc({primary_key, deterministic_key, key_derivation_salt});
auto url = enc.decrypt(stored, rails::Scheme::Deterministic); // encrypts :x, deterministic: true
auto raw = enc.decrypt(stored, rails::Scheme::Primary);       // encrypts :x
auto out = enc.encrypt(raw, rails::Scheme::Primary);
```

Errors never contain plaintext, ciphertext or keys. Envelope encryption and key references are not supported.
