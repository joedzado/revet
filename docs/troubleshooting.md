# Troubleshooting

When something goes wrong, in roughly the order it tends to go wrong.

## "My framework change isn't taking effect"

Symptom: you edited `~/revet/cmake/gen_routes.py` (or anything in the framework source tree), ran `revet dev`, the change didn't appear.

Cause: `revet dev` reads the framework from `~/.revet/current/`, which is populated by `revet-fw install` at install time. Editing the source repo does not redeploy.

Fix:

```bash
sudo ~/revet/scripts/revet-fw install
```

This wipes `~/.revet/current/` and copies the source over. Re-run `revet dev` afterwards.

(If you're hacking on the framework frequently, point `FRAMEWORK_DIR` in your project's `CMakeLists.txt` at your source checkout. Don't ship a project that does this.)

## `conan: command not found`

Conan is the C++ package manager revet uses to install libsodium, libcurl, simdjson, ctre. Install it:

```bash
pip3 install conan          # or: pip3 install --break-system-packages conan
conan profile detect        # creates ~/.conan2/profiles/default
```

Then `revet build` again — the first build will fetch and compile dependencies (one-time, a few minutes).

## `conan install failed`

The dependency install died part-way. Common causes:

- **No internet / Conan Center unreachable.** Retry on a different network.
- **Compiler mismatch.** Your local clang/gcc disagrees with the cached Conan profile. Regenerate: `rm -rf ~/.conan2 && conan profile detect`.
- **Disk full** in `~/.conan2/`.

The framework always installs Release-config packages (`-s build_type=Release`). Debug builds get them via `CMAKE_MAP_IMPORTED_CONFIG_DEBUG=Release`. If you need genuinely Debug-built deps (rare — only for stepping into them in lldb), you're off the happy path.

## Build error: `'concept' is not a class or namespace`, or similar C++23 errors

revet requires a C++23-capable compiler. macOS users: Apple clang on Xcode 16+ works for most of the surface; for the rest, install LLVM 19 via Homebrew (`brew install llvm`) and:

```bash
export CC=$(brew --prefix llvm)/bin/clang
export CXX=$(brew --prefix llvm)/bin/clang++
revet clean && revet build
```

Linux: clang 18+ or gcc 14+.

## "Built target revet" but `./build/revet` exits immediately

Check `stderr` — the framework logs to stderr only. A common silent killer:

- **`REVET_COOKIE_KEY` set to a malformed value** (not 64 hex chars). Throws `std::runtime_error` from `crypto::get_key` at first cookie use, not at startup. Set it to a real 64-hex string or unset it for hostname-derived dev key.

## `bind: Address already in use`

Another process is on your port (default 8080).

```bash
lsof -i :8080                 # show the process
revet dev --port 8090         # or pick a different port
```

If you killed the previous `revet dev` ungracefully, the socket may sit in `TIME_WAIT` for ~60 s (a kernel cool-down state that holds the port after a noisy disconnect, to drop any straggling packets). Wait, or pick a different port.

## Browser shows the wrong template / stale CSS

revet links static assets into the binary. After editing CSS-in-C++ or any HTML/JS asset, you need to rebuild — `revet dev` watches sources and re-builds automatically, but if you've manually overridden the binary (e.g. running `./build/revet` directly), restart it.

## `Set-Cookie` not setting on the browser

- Browser dropped the cookie because the request was over `http://` but `Secure` was set. Either turn `REVET_FORCE_SECURE_COOKIES=0` for local dev, or use HTTPS.
- Browser dropped the cookie because of `SameSite=Lax` on a cross-site form post. Expected behavior — keep your auth flows same-site.

## CSRF: every POST returns 403

- Form is missing the hidden CSRF input. Use `web::csrf::field(req)` inside `html::form{...}`.
- HTMX requests are missing the header. Drop `web::csrf::htmx_headers(req)` on `<body>`.
- Cookie was issued by a different `REVET_COOKIE_KEY` (e.g. you rotated the key, or moved between dev hosts). Clear cookies and reload.

## SQLite: `database is locked`

Multiple writers contending under a non-trivial load. The framework's `db::sqlite::Pool` serializes writes through a `Handle<db::Write>` mutex, but if you bypass that or hold a `Handle<Write>` across a long `co_await`, the lock can be held longer than expected. Audit any handler that does `co_await` between acquiring a write handle and releasing it.

## Postgres: `connection refused` / `password authentication failed`

revet does no extra parsing of `DATABASE_URL` — it hands the URL to libpq. So `psql "$DATABASE_URL"` should work too; if it doesn't, the issue is the URL or the server, not revet.

## Graceful shutdown

Today revet does not handle `SIGTERM` to drain in-flight requests. `systemctl stop` cuts active connections. If this matters for your app (long SSE streams, slow upstream calls), drain at the reverse proxy first (e.g. nginx `add_header Connection close;` + temporarily route around the instance) before stopping the binary. A first-class drain story is on the roadmap.

## Tests fail with `crypto::get_key: REVET_COOKIE_KEY ...`

Test fixtures should not require `REVET_COOKIE_KEY` — when unset, the framework derives a key from `hostname()`. If you see a hard requirement in a test, check whether the test is setting an env var that pretends to be a key but isn't 64 hex chars.

## `revet new myapp` then `revet dev` produces a wall of warnings

Default user code compiles without `-Werror`. If warnings actually break your build, something else is wrong — read the warnings. Production builds turn on `-Werror` with `cmake -DREVET_STRICT=ON`.
