# Architecture

A 10-minute tour of how a revet binary is put together and how a request flows through it.

## Build-time pipeline

```
   ┌─────────────────────────────────────────────┐
   │  Your app source                            │
   │  ─────────────                              │
   │  app/                                       │
   │    routes/   *.cpp     ─┐                   │
   │    apis/     *.hpp     ─┤                   │
   │    models/   *.hpp     ─┤                   │
   │    queries/  *.sql     ─┤                   │
   │    forms/    *.hpp     ─┼─►  gen_*.py       │
   │    states/   *.hpp     ─┤   (15 scripts;    │
   │    layouts/  *.cpp     ─┤    see            │
   │    components/ *.cpp   ─┤    codegen.md)    │
   │    locale/   *.csv     ─┤                   │
   │    public/             ─┘                   │
   │  revet.toml            ─►                   │
   └─────────────────────────┬───────────────────┘
                             │
                             ▼
              build/generated/*.hpp
              build/generated/routes.cpp
                             │
                             ▼
              clang++ -std=c++23   (your code + generated + framework src/*.cpp)
                             │
                             ▼
                       build/revet           (single executable)
```

The codegen scripts are independent — most can run in parallel. The only ordering is established by `#include` dependencies in the generated files (e.g. `apis.hpp` includes `models.hpp`).

## Runtime architecture

```
   Listener thread (1)                  bound to ServerConfig.port
        │  accept()
        ▼
   Request thread pool (N)              N = ServerConfig.request_threads
   ──────────────────                     (default: hardware_concurrency())
   Each thread:
     1. parse HTTP request
     2. Context::current() set up (thread_local)
     3. middleware chain runs (CSRF, custom guards, …)
     4. route handler invoked
            │
            │   sync handler returns Response   ┐
            │                                   │
            │   async handler returns           │  if any co_await happens:
            │   Task<Response>                  │     handler is suspended,
            │                                   │     resumes on an IO thread.
            ▼                                   │
     5. response serialized to socket           │
                                                ▼
                                       IO thread pool (M)
                                       ─────────────────
                                       M = ServerConfig.io_threads
                                         (default: 4 × cores)
                                       Runs:
                                         - http::Awaitable (outbound HTTP)
                                         - async_query / async_exec (DB)
                                         - web::run_on_io(...)
```

Request threads are CPU-bound (HTML rendering, JSON serialization). IO threads are I/O-bound (blocking syscalls in libcurl, libsqlite, libpq). A coroutine that `co_await`s shifts to the IO pool, runs the blocking call, and resumes back on whichever thread the dispatcher picks up — *not* necessarily the original request thread. (`co_await` pauses the function until the awaited operation finishes, similar to `await` in JS, except the framework picks which thread resumes you.) Beware of thread-local state across `co_await`.

## Key abstractions

### `web::Task<T>`

Header-only coroutine type, `task.hpp`. Eager-suspending — the coroutine body doesn't start until something awaits it. Exceptions thrown inside propagate through `co_await`. Tests in `tests/test_task.cpp` lock the contract.

### `web::Context::current()`

Thread-local context. Cleared at the start of every request. Holds:
- `states` — a `flat_map<type_index, any>` for `State<T>`, `Flash<T>`, and the CSRF token.
- `files` — uploaded file metadata after `uploads::allow(...)`.
- Other per-request scratch.

`co_await` may resume on a different thread; `Context::current()` will be the *new* thread's context, not the original. State that needs to survive a `co_await` must be captured into the lambda/coroutine frame.

### `http::Awaitable<T>` / `http::get/post/...`

Wraps a `Client::execute` call. `await_suspend` posts the call to the IO pool; `await_resume` returns the `Response`. The high-level generated `apis.hpp` wraps this further to throw `http::Error` on non-2xx.

### Pools (`db::sqlite::Pool` / `db::postgres::Pool`)

Live as `static` inside generated `resources::_pools::_pool_<name>()`. Created at first use, never destroyed (process-lifetime). `Handle<Intent>` is a RAII acquire from the pool (acquired in the constructor, released in the destructor — when the variable goes out of scope, the connection returns to the pool automatically); `Handle<Write>` additionally takes a serializing mutex so writers don't race.

## File layout

```
/Users/joedzado/revet/
├─ CMakeLists.txt        # framework's own build (revet_core, tests)
├─ VERSION               # single source of truth for the version string
├─ bin/revet             # user-facing CLI (Python)
├─ cmake/                # codegen scripts + shared helpers
├─ include/revet/        # all header-only framework code
├─ src/                  # framework .cpp (http_client, json, ...)
├─ static/               # htmx, optimistic.ts → inlined into binary
├─ template/             # what `revet new` copies into a new project
├─ demo/                 # showcase site that exercises every feature
├─ tests/                # framework tests (catch2 + Python unittest)
├─ docs/                 # this directory
└─ scripts/revet-fw      # framework dev/install commands
```

When you build:
- Framework's own CMakeLists is for hacking on the framework (`revet-fw test`).
- `template/CMakeLists.txt` is what gets copied per-project by `revet build`.
- `demo/CMakeLists.txt` is the demo's own copy.

If you change build behavior, edit `template/CMakeLists.txt`. The demo's copy regenerates from it.

## Distribution & install

`scripts/revet-fw install` does two things:

1. Copies the source tree to `~/.revet/current/` so the CLI can find it from `FRAMEWORK_DIR = "/Users/joedzado/.revet/current"` (baked into the per-project CMakeLists).
2. Symlinks `~/.revet/current/bin/revet` to `/usr/local/bin/revet`.

The CLI (`bin/revet`) reads `VERSION` from its parent directory, so it correctly reports the *installed* version even if you have an unsynced source repo elsewhere.

End-users install via the curl-pipe-to-sh installer (`install.sh`); contributors who hack the framework re-run `revet-fw install` after edits.
