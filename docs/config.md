# Configuration

revet apps are configured in three places:

1. **`revet.toml`** — declarative project config: name, port, resources, uploads, logging.
2. **Environment variables** — secrets, deploy-time flags, log level override.
3. **`web::config::*` / CLI flags** — runtime overrides for the binary.

## `revet.toml`

```toml
name = "myapp"           # required; binary identity + cookie scope
port = 8080              # default 8080; overridden by --port / web::config
max_body = "64MB"        # largest request body read, default 64 MiB; "512KB", "1GB" or bytes

# Logging
[logging]
enabled = true           # set false to disable all log output
level    = "info"        # debug | info | warn | error  (overridden by LOG_LEVEL env)

# File uploads — declare one block per bucket
[uploads.files]
# incoming  = "uploads/files/incoming"   # defaulted; auto-created at build, mode 0700
# validated = "uploads/files/validated"  # defaulted; auto-created at build, mode 0750
# max_bytes = 10485760                   # default 10 MiB

# Databases — declare one [resources.<name>] block per pool
[resources.users_db]
type      = "sqlite"     # "sqlite" or "postgres"
path      = "build/data/users.db"   # sqlite only
pool_size = 4

[resources.events_db]
type = "postgres"
url  = "env:DATABASE_URL"   # `env:NAME` reads the value from environment

# External Conan dependencies — auto-installed by `revet build`
[build.dependencies]
# zlib = "1.3.1"
# nlohmann_json = "3.11.3"

# Pinning htmx (vendored into the binary)
[htmx]
# version = "2.0.4"        # default tracks the framework's bundled htmx

# Per-toggle defaults (gen_toggles.py)
[toggles]
# default_dark_mode = true
```

## Environment variables

| Variable | Read by | Effect | Required in prod |
|---|---|---|---|
| `REVET_COOKIE_KEY` | `crypto.hpp` | 64-char hex (32 bytes) AEAD key for state + CSRF cookies. Without it, derives a key from `hostname()` — fine for dev, never for prod. | **Yes** |
| `REVET_FORCE_SECURE_COOKIES` | `web.hpp` | When `=1`, all `Set-Cookie` headers include the `Secure` attribute. Off by default for localhost dev. | **Yes** (when serving HTTPS) |
| `REVET_MAX_BODY` | `web.hpp` | Largest request body in bytes or with a binary unit (`64MB`, `1GB`). Overrides `revet.toml max_body`. Bigger bodies get 413. | No |
| `LOG_LEVEL` | `log.hpp` | Overrides `revet.toml [logging].level`. Values: `debug`, `info`, `warn`, `error`. | No |
| `DATABASE_URL` (or any name you reference) | `db/postgres.hpp` via `url = "env:DATABASE_URL"` | Resolves the postgres connection string at startup. | If using postgres |
| `API_KEY` (or any name) | User code via `bearer_static(std::getenv("API_KEY") ?: "")` | Whatever your API guard reads. | If using API auth |

## CLI flags and `web::config`

```cpp
int main(int argc, char** argv) {
    web::init(argc, argv);                 // parses --port / --request-threads / --io-threads
    web::config::secure_cookies(true);     // optional override
    web::listen(routes::all());
}
```

| Flag | Programmatic | Default | Notes |
|---|---|---|---|
| `--port N` / `-p N` | `ServerConfig.port` | 8080 (or `revet.toml`) | TCP listen port |
| `--request-threads N` | `ServerConfig.request_threads` | `hardware_concurrency()` (number of logical CPU cores — e.g. 10 on an M1 Pro) | CPU-bound work — route handlers, codegen wrappers. Don't oversubscribe past cores. |
| `--io-threads N` | `ServerConfig.io_threads` | `4 × hardware_concurrency()` | I/O-bound work — `co_await http::get`, `async_query/async_exec`, etc. Bumped for high-fan-out outbound HTTP. |
| `--max-body SIZE` | `web::config::max_body_bytes(size_t)` | 64 MiB (or `revet.toml` / env) | Request bodies are buffered in memory, so this bounds memory per request. Over the limit → 413. |
| — | `web::config::secure_cookies(bool)` | `false` (or env) | See above. |

## Precedence

For any setting that has multiple sources, later layers override earlier ones:

```
revet.toml  <  environment variable  <  web::config:: setter  <  CLI flag
```

So `LOG_LEVEL=debug ./revet` beats `[logging] level = "info"`; `./revet --port 9000` beats both env and toml.

## Tuning `request_threads` / `io_threads`

- **`request_threads`** is the size of the main worker pool. One CPU core per thread is the right starting point. Going higher only helps when route handlers spend time blocked on something the OS can't tell about (rare in revet — coroutines move work to `io_threads`).
- **`io_threads`** runs anything you `co_await`. Outbound HTTP, async DB queries, SSE detached threads. The default (4× cores) is sized for moderate fan-out; if you proxy heavily to slow upstreams, raise it. Watch for `EMFILE` (too many file descriptors) — that's the real ceiling.
- Profile before tuning. `revet dev --profile` records a flame graph via samply.
