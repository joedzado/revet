# revet

A C++23 server-rendered web framework with file-based routing and modern ergonomics. Build full-stack web applications with the simplicity of JavaScript frameworks, running entirely on the server.

> **Status:** Experimental.

> **Topical docs** for installation, deployment, security, troubleshooting, codegen reference, and contributor onboarding live in [docs/](docs/README.md).

## Contents

- [Features](#features)
- [Coming from JavaScript?](#coming-from-javascript)
- [Design Principles](#design-principles)
- [Quick Start](#quick-start)
- [Project Structure](#project-structure)
- [Core Concepts](#core-concepts)
  - [Routes](#routes) · [HTML DSL](#html-dsl) · [SVG DSL](#svg-dsl) · [CSS](#css) · [Type-safe URLs](#type-safe-urls) · [Components](#components) · [Layouts](#layouts)
  - [Databases & Queries](#databases--queries) · [Input Validation](#input-validation) · [Sessions & Cookies](#sessions--cookies)
  - [Middleware & Guards](#middleware--guards) · [CSRF](#csrf) · [File Uploads](#file-uploads)
  - [Server-Sent Events](#server-sent-events) · [UI Toggles](#ui-toggles) · [i18n](#i18n)
  - [HTMX](#htmx) · [JSON API Routes](#json-api-routes) · [Outbound HTTP API Calls](#outbound-http-api-calls)
  - [Error Handlers](#error-handlers) · [Static File Serving](#static-file-serving) · [Logging](#logging)
- [Zero Raw Strings](#zero-raw-strings)
- [Async Model](#async-model)
- [Development Workflow](#development-workflow)
- [Quick Reference](#quick-reference)
- [Framework Development](#framework-development)
- [Contributing](#contributing) · [License](#license)

## Features

- **File-based routing** — `app/routes/` file path = URL path; no routing configuration needed
- **XSS-safe HTML DSL** — typed, composable HTML generation; all content escaped by default; `html::raw()` as explicit escape hatch
- **Type-safe codegen** — routes, CSS classes, SQL queries, i18n strings, and outbound HTTP calls all produce typed C++ constants and functions — no raw strings at call sites
- **SQL query codegen** — write `.sql` files; get typed async `queries::ns::fn()` functions with pooled connections (SQLite + PostgreSQL)
- **JSON API routes** — typed model params/returns; bearer auth; versioning via directory nesting
- **Outbound HTTP API codegen** — `[[revet::external]]` declarations in `app/apis/*.hpp` → typed async coroutines
- **Encrypted sessions & flash** — libsodium XChaCha20-Poly1305; no session database; state and flash injected directly into handler params
- **HTMX integration** — server-driven partial updates with compile-time–checked optimistic updates; CSRF auto-injected
- **UI toggles** — boolean cookie state (sidebars, dark mode) with zero JS and zero boilerplate
- **Server-Sent Events** — long-lived streaming connections in detached threads
- **File uploads** — magic-byte type detection; composable validators; path-jailed destination dirs
- **i18n** — CSV string tables → compile-time `i18n::` constants
- **CSRF, input validation, middleware, per-route guards** — all first-class
- **Dev server** — file watcher with auto-rebuild; ASan, profiler, and debugger flags
- **Single binary** — `revet build` produces a statically-linked executable; no runtime, no Node

revet is server-first. Most interactivity — form submissions, togglable UI, flash messages, partial page updates, optimistic mutations — is handled entirely server-side. For functionality that genuinely requires client-side JavaScript, [HTMX](https://htmx.org) is the preferred integration path; raw `<script>` tags are always available for anything beyond that.

## Coming from JavaScript?

revet borrows the ergonomics of Next.js/Remix but runs as a compiled C++ binary. The mental model maps closely:

| JavaScript / Next.js | revet |
|----------------------|-------|
| `app/page.tsx` | `app/routes/index.cpp` |
| `app/users/[id]/page.tsx` | `app/routes/users/[id].cpp` |
| `app/layout.tsx` | `app/layouts/main.cpp` with `[[revet::export]]` |
| CSS Modules / Tailwind | `app/styles/*.css` → `styles::class_name` constants |
| Server Actions / API routes | `render_post()` in the same route file |
| `getServerSideProps` | everything in revet is server-side by default |
| `useSession` / next-auth | `web::State<Session>` injected into handler params |
| Toast / flash messages | `web::Flash<Toast>` — one-shot encrypted cookie |
| Prisma / Drizzle | `app/queries/*.sql` → `queries::ns::fn()` codegen |
| React components | `[[revet::export]]` functions in `app/components/` |
| `next/router` `push` | `web::redirect(urls::users)` |
| `process.env.SECRET` | `REVET_COOKIE_KEY` env var |
| `npm run dev` | `revet dev` |
| `npm run build` | `revet build` |

The main difference: no JavaScript, no runtime, no Node. One binary. `revet build` produces a single statically-linked executable ready to deploy.

## Design Principles

- **You only pay for what you use** — Features not declared in `revet.toml` produce zero code, zero link dependencies, zero runtime overhead
- **Convention over configuration** — Sensible defaults everywhere; override only when necessary
- **Zero boilerplate in user code** — The framework generates routing glue; route files are pure functions
- **Modern C++23 throughout** — `auto` parameters, designated initializers, `std::expected`, linter-backed safety

## Quick Start

### Install

```bash
curl -fsSL https://raw.githubusercontent.com/joedzado/revet/main/install.sh | sh
```

This installs dependencies and the `revet` CLI. Requires CMake ≥ 3.20, clang++ ≥ 17 (or g++ ≥ 13), and Python 3.8+.

> **Note for C++ developers:** revet uses CMake and Conan under the hood, but you won't touch `CMakeLists.txt` directly — the CLI manages the entire build. `revet new` scaffolds the project, `revet dev` builds and runs it, and `revet build` produces the release binary.

<details>
<summary>Manual installation</summary>

```bash
git clone https://github.com/joedzado/revet.git
cd revet
./scripts/install_deps.sh        # install system deps
sudo ./scripts/revet-fw install  # deploy to ~/.revet/current/, symlink revet to /usr/local/bin
```
</details>

### Create & Run a Project

```bash
revet new myapp
cd myapp
revet dev        # http://localhost:8080
```

### Deploy

```bash
revet build
# Binary at ./build/revet — ready to deploy
```

## Project Structure

```
myapp/
├── revet.toml               ← Project config (name, port, logging, dependencies)
├── app/
│   ├── routes/              ← Route handlers (file path = URL path)
│   ├── layouts/             ← Page-level HTML wrappers
│   ├── components/          ← Reusable UI components
│   ├── styles/              ← CSS (inlined at compile time)
│   ├── queries/             ← SQL queries (auto-generated bindings)
│   ├── forms/               ← Form structs with [[revet::form]] annotations
│   └── states/              ← Encrypted cookie state and UI toggles
│   └── apis/                ← Outbound HTTP API declarations ([[revet::external]])
├── locale/                  ← i18n translation strings (.csv)
├── models/                  ← Typed data models (auto-serialization)
├── src/                     ← main.cpp, site_init.cpp, helpers
└── tests/                   ← Test files (optional)
```

### Folder reference

Each folder has a specific file type convention and codegen role. All generated files land in `build/generated/`.

| Folder | Files | Generates | Description |
|--------|-------|-----------|-------------|
| `app/routes/` | `.cpp` | `routes.hpp`, `routes.cpp` | One file per URL route. File path = URL path. |
| `app/layouts/` | `.cpp` | `components.hpp` | Page wrappers with `[[revet::export]]`. |
| `app/components/` | `.cpp`, `.hpp` | `components.hpp` | `.cpp` for exported UI components; `.hpp` for shared helpers auto-included everywhere. |
| `app/styles/` | `.css` | `styles.hpp` | CSS classes become `styles::name` constants. |
| `app/queries/` | `.sql` | `queries.hpp` | SQL queries become async `queries::ns::fn()` functions. |
| `app/forms/` | `.hpp` | `forms.hpp` | Form structs with `[[revet::form]]` field annotations. |
| `app/states/` | `.hpp` | `states.hpp`, `toggles.hpp` | Encrypted cookie state and boolean UI toggles. |
| `app/apis/` | `.hpp` | `apis.hpp` | Outbound HTTP — `[[revet::external]]` function decls become async `namespace::fn()` coroutines. |
| `models/` | `.hpp` | `models.hpp` | Typed JSON/DB model structs with `[[revet::model]]`. |
| `locale/` | `.csv` | `i18n.hpp` | Translation strings become `i18n::key` constants. |
| `src/` | `.cpp`, `.hpp` | (compiled directly) | App utilities. Not auto-included. |

## Core Concepts

### Routes

Routes are C++ files in `app/routes/`. The filename and directory structure map directly to URL paths.

| File | URL |
|------|-----|
| `app/routes/index.cpp` | `/` |
| `app/routes/users.cpp` | `/users` |
| `app/routes/users/new.cpp` | `/users/new` |
| `app/routes/users/[id].cpp` | `/users/:id` |
| `app/routes/users/[id]/delete.cpp` | `/users/:id/delete` |

Static routes win over parameterized — `/users/new` matches before `/users/:id`.

```cpp
// app/routes/index.cpp
#include <revet/web.hpp>

static auto render_get(const auto& req) {
    return layout_main(req, "Home", html::h1{ "Hello" });  // layout_main defined in app/layouts/
}
```

```cpp
// app/routes/users/[id].cpp
static auto render_get(const auto& req) {
    auto id = req.params.get("id");
    return layout_main(req, "User", html::div{ id });  // layout_main defined in app/layouts/
}
```

Define additional HTTP method handlers in the same file: `render_post`, `render_put`, `render_delete`, `render_patch`.

Async handlers (required for DB/HTTP calls):

```cpp
static auto render_get(const web::Request& req) -> web::Task<web::Response> {
    auto users = co_await queries::users::all(db);
    co_return layout_main(req, "Users", html::ul{
        html::nodes(users, [](const User& u) { return html::li{ u.name }; })
    });
}
```

### HTML DSL

Type-safe HTML generation. All text content and attribute values are XSS-escaped by default. `html::raw()` is the explicit opt-out.

```cpp
#include <revet/html.hpp>

html::div{
    html::attr.id("main"),
    styles::card,
    html::h2{ "Title" },
    html::p{ user.bio },          // user input — auto-escaped
    html::a{ html::attr.href(urls::about), "About" },
    html::raw(trusted_html),      // explicit opt-out — bypasses escaping
}
```

**Self-closing elements:** `html::input`, `html::br`, `html::img`, `html::hr`, `html::meta`, `html::link`

**Group without wrapper:** `html::fragment(node1, node2, ...)`

**Conditional:**
```cpp
logged_in ? html::p{ "Welcome" } : html::Node{}
```

**List from data:**
```cpp
html::ul{
    html::nodes(users, [](const User& u) { return html::li{ u.name }; })
}
```

**URL safety:** `attr.href()` and `attr.src()` pass through `sanitize::safe_url()`, which blocks `javascript:`, `data:`, `vbscript:` and allows only `http`, `https`, `mailto`, `tel`, and relative paths.

### CSS

CSS files in `app/styles/` are parsed at build time into a `styles::` namespace and inlined into the page `<head>` as a `<style>` block. No separate CSS file is served at runtime.

```css
/* app/styles/app.css */
.card { padding: 1rem; border-radius: 0.5rem; }
.btn  { ... }
.btn-primary { ... }
```

```cpp
html::div{ styles::card, html::button{ styles::btn, styles::btn_primary, "Submit" } }
```

Multiple `styles::` constants compose automatically into a single `class="..."` attribute.

**Dynamic classes:**
```cpp
html::a{ styles::sidebar_link, is_active ? styles::active : html::Node{}, label }
```

**Inline CSS class** (component-local styles, no CSS file needed):
```cpp
static const auto style = html::css_class({
    .padding       = "0.5rem 1rem",
    .border_radius = css::radius::full,
    .background    = "var(--accent-dim)",
    .color         = "var(--accent)",
});
return html::span{ style, content };
```

Use `styles::` for app-wide classes. Use `html::css_class` for classes local to a single component.

### SVG DSL

`#include <revet/svg.hpp>` gives a typed SVG namespace that mirrors the HTML DSL — all attribute values are escaped, no `html::raw()` needed for inline SVG.

```cpp
#include <revet/svg.hpp>

// Heroicon: bars-3 (hamburger menu)
svg::svg{
    svg::attr.viewBox("0 0 24 24"),
    svg::attr.width("24"),
    svg::attr.height("24"),
    svg::attr.aria_label("Menu"),
    svg::attr.fill("none"),
    svg::attr.stroke("currentColor"),
    svg::attr.stroke_width("2"),
    svg::attr.stroke_linecap("round"),
    svg::path{ svg::attr.d("M4 6h16M4 12h16M4 18h16") },
}
```

**Container elements:** `svg::svg`, `svg::g`, `svg::defs`, `svg::symbol_el`, `svg::text`, `svg::tspan`, `svg::clipPath`, `svg::mask`, `svg::marker`, `svg::pattern`, `svg::linearGradient`, `svg::radialGradient`, `svg::stop`, `svg::title`

**Leaf elements (self-closing):** `svg::path`, `svg::circle`, `svg::ellipse`, `svg::rect`, `svg::line`, `svg::polyline`, `svg::polygon`, `svg::use_el`, `svg::image`

**Attribute helpers:** `svg::attr.fill`, `.stroke`, `.stroke_width`, `.stroke_linecap`, `.stroke_linejoin`, `.fill_rule`, `.opacity`, `.width`, `.height`, `.x`, `.y`, `.rx`, `.ry`, `.cx`, `.cy`, `.r`, `.x1`, `.y1`, `.x2`, `.y2`, `.d`, `.points`, `.viewBox`, `.transform`, `.preserveAspectRatio`, `.offset`, `.stop_color`, `.stop_opacity`, `.gradientUnits`, `.id`, `.class_`, `.style`, `.href`, `.aria_label`, `.xmlns`

### Type-safe URLs

Never hard-code path strings. Codegen produces `urls::` constants from `app/routes/`. Auto-included in all route files — no `#include` needed.

```cpp
// Static routes
web::redirect(urls::users);
html::attr.href(urls::users_new);

// Parameterized — app/routes/users/[id].cpp → urls::users__id_(id)
html::attr.href(urls::users__id_(user.id));
html::attr.action(urls::users__id__delete(user.id));
```

Naming: path segments joined by `_`, dynamic `[param]` becomes `_param_`. Toggle routes are also included (see [UI Toggles](#ui-toggles)).

### Components

Reusable UI building blocks. Declare with `[[revet::export]]` in `app/components/`. Auto-included in all route files — no `#include` needed.

```cpp
// app/components/badge.cpp
[[revet::export]]
html::Node badge(std::string_view label) {
    return html::span{ styles::badge, label };
}

// Use directly in any route
badge("live")
```

### Layouts

Master templates for consistent page structure.

```cpp
// app/layouts/main.cpp
[[revet::export]]
web::Response layout_main(const web::Request& req, std::string_view title, html::Node inner) {
    return web::to_response(html::html_doc{
        html::head{ html::title{ title }, html::collect_styles() },
        html::body{
            html::header{ "Navigation" },
            inner,
            html::footer{ "© 2026" }
        }
    });
}
```

### Databases & Queries

End-to-end typed database access: declare connections in `revet.toml`, write plain SQL files, define model structs — the framework generates all the glue.

#### 1. Declare a database resource

```toml
# revet.toml
[resources.mydb]
type = "sqlite"          # or "postgres"
path = "data/myapp.db"  # postgres: url = "env:DATABASE_URL"
pool_size = 4
```

Generates a `resources::mydb<Intent>()` factory:

```cpp
auto db = resources::mydb();               // read-only handle
auto db = resources::mydb<db::Write>();    // write handle (acquires write lock)
```

#### 2. Write SQL queries

Place `.sql` files in `app/queries/`. Each `-- @revet:name:` block becomes a C++ function in a namespace matching the filename.

```sql
-- app/queries/users.sql

-- @revet:name: all
-- @revet:model: User
SELECT id, name, email FROM users;

-- @revet:name: find_by_id
-- @revet:model: User
-- @revet:param: id int
SELECT id, name, email FROM users WHERE id = :id LIMIT 1;

-- @revet:name: create
-- @revet:param: name string
-- @revet:param: email string
INSERT INTO users (name, email) VALUES (:name, :email);
```

Use in handlers:

```cpp
// Async (preferred in web handlers)
auto users = co_await queries::users::all(db);
auto found = co_await queries::users::find_by_id(db, std::stoi(id));  // single param: direct
co_await queries::users::create(db, {.name = name, .email = email});  // multi-param: struct

// Sync (layouts, site_init, non-coroutine helpers)
auto count = queries::users::sync_count(db);
```

#### 3. Define model structs

Place structs annotated with `[[revet::model]]` in `models/`. The framework generates `from_json<T>`, `to_json<T>`, and `from_row<T>` for each.

```cpp
// models/user.hpp
struct [[revet::model]] User {
    std::string id;
    std::string name;
    std::string email;
    double      score;
    int64_t     created_at;
};
```

Add `-- @revet:model: TypeName` to any SQL block and the generated function returns `std::vector<TypeName>` directly. Column order in `SELECT` must match field declaration order in the struct.

#### 4. JSON serialization

```cpp
#include "models.hpp"

User u = revet::json::from_json<User>(doc);   // parse JSON document
std::string s = revet::json::to_json(u);      // serialize to JSON string
```

### Input Validation

`validate::check()` returns `std::expected<void, std::vector<Error>>` — the compiler forces you to handle the error case.

```cpp
#include <revet/validate.hpp>

auto result = validate::check(req.form, validate::schema(
    validate::field("email",
        validate::required,
        validate::max_len<254>,
        validate::regex<"[^@]+@[^@]+\\.[a-z]+">
    ),
    validate::field("message",
        validate::required,
        validate::min_len<10>
    )
));

if (!result) {
    for (const auto& e : result.error())
        log::warn("validate: {}: {}", e.field, e.message);
}
```

Access form fields: `req.form.get("email")` — returns `std::string`, empty if missing.

### Sessions & Cookies

State is stored in encrypted cookies (libsodium XChaCha20-Poly1305). No session database needed.

#### State cookies

```cpp
// models/session.hpp
struct [[revet::model]] Session { std::string user_id; };

// After login: attach state to the response cookie
static auto render_post(const web::Request& req) -> web::Response {
    return web::redirect(urls::dashboard).state(Session{ .user_id = "123" });
}

// In protected routes: state is injected automatically
static auto render_get(const web::Request& req, web::State<Session> session) {
    if (!session) return web::redirect(urls::login);
    // session->user_id
}
```

#### Flash messages

One-shot: set on redirect, read once in the next request, then cleared.

```cpp
// Set on redirect
return web::redirect(urls::users).flash(Toast{ .message = "Created.", .kind = "success" });

// Read in next handler
static auto render_get(const web::Request& req, web::Flash<Toast> flash) {
    if (flash) { /* flash->message, flash->kind */ }
}
```

`web::State<T>` and `web::Flash<T>` can both appear as parameters in the same handler.

### Middleware & Guards

```cpp
// Global middleware (site_init.cpp)
web::use([](const web::Request& req, const web::Next& next) {
    log::info("{} {}", req.method, req.path);
    return next();
});

web::use(web::csrf::middleware);

// Per-route guard — runs before global middleware and before state injection.
// Guards check cookies directly; web::State<T> is not yet populated at this point.
// app/routes/admin/dashboard.cpp
static auto route_guards() -> std::vector<web::Middleware> {
    return {
        [](const web::Request& req, web::Next next) -> web::Response {
            if (req.cookies.get("_cw_Session").empty())
                return web::redirect(urls::login);
            return next();
        }
    };
}
```

### CSRF

```cpp
// Enable globally
web::use(web::csrf::middleware);

// In forms
html::form{ web::csrf::field(req), ... }

// In HTMX (drop on <body> to cover the whole page)
html::body{ web::csrf::htmx_headers(req), ... }
```

Blocks POST, PUT, PATCH, DELETE without a valid token.

**Cookie format.** The `_csrf` cookie holds the token **sealed with the framework's AEAD key** (`REVET_COOKIE_KEY` — XChaCha20-Poly1305). The raw token only ever appears in form fields / `x-csrf-token` headers. This kills the classic double-submit-cookie weakness where a malicious subdomain could set its own `_csrf` cookie on the parent domain and forge a matching token — without the key it can't produce a valid ciphertext, and any tampered cookie fails AEAD verification → 403.

**Constant-time compare.** Token equality uses `web::crypto::secure_eq` (libsodium `sodium_memcmp`), not `==`. Never compare any secret with `==` in your code — use the same helper.

### Secure cookies

Set-Cookie headers always carry `HttpOnly; SameSite=Lax`. The `Secure` flag is **opt-in**: turn it on at startup for HTTPS deployments.

```cpp
int main(int argc, char** argv) {
    web::init(argc, argv);
    web::config::secure_cookies(true);   // or set REVET_FORCE_SECURE_COOKIES=1
    web::listen(routes::all());
}
```

Off by default so localhost dev over plain HTTP receives cookies; browsers drop `Secure` cookies on `http://`.

### File Uploads

Declare an uploads bucket in `revet.toml`. The framework creates both directories at configure time with restrictive permissions.

```toml
[uploads.files]
# incoming  = "uploads/files/incoming"   # auto-created, mode 0700 (tmp landing zone)
# validated = "uploads/files/validated"  # auto-created, mode 0750 (final destination)
```

`uploads::allow` validates every file via magic-byte detection (not `Content-Type`), then atomically moves each file to the validated dir. On any failure it deletes all tmp files and returns 422 — your handler never runs.

```cpp
#include <revet/upload.hpp>

static auto route_guards() -> std::vector<web::Middleware> {
    return {
        uploads::allow({
            .validated_dir = std::filesystem::path(uploads::files::validated_dir),
            .types         = { web::FileType::jpeg, web::FileType::png,
                               web::FileType::gif,  web::FileType::webp },
            .validators    = { uploads::max_size(5 * 1024 * 1024) },  // 5 MB cap
        })
    };
}

static auto render_post(const web::Request&) -> web::Response {
    auto& files = web::Context::current().files;
    if (files.empty()) return web::redirect(urls::upload);
    // files[0].path          — points into validated_dir
    // files[0].filename      — sanitized original filename
    // files[0].detected_type — from magic bytes
    return web::redirect(urls::upload);
}
```

Form must use `enctype="multipart/form-data"`:

```cpp
html::form{
    html::attr.action(urls::upload),
    html::attr.method("POST"),
    html::attr.enctype("multipart/form-data"),
    web::csrf::field(req),
    html::input{ html::attr.type("file").name("avatar").accept("image/*") },
    html::button{ html::attr.type("submit"), "Upload" },
}
```

Custom validators: any `std::function<bool(web::UploadedFile&)>`. Return `false` to reject — middleware deletes all tmp files and returns 422.

The `validated_dir` is jailed to the project root. World-readable destination directories are rejected.

### Server-Sent Events

```cpp
// app/routes/events.cpp — handler signature triggers SSE registration
static auto render_sse(const web::Request& req, web::SSEWriter& w) -> void {
    while (w.alive()) {
        w.send("payload");                         // data only
        w.send("payload", "event-name");           // data + event type
        w.send("payload", "event-name", "id-1");  // data + event type + id
        w.comment();                               // keepalive ping
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
```

SSE routes run in a detached thread — not tied to either thread pool. Client-side:

```js
const es = new EventSource("/events");
es.addEventListener("event-name", e => console.log(e.data));
```

### UI Toggles

Boolean UI state (sidebar open/closed, dark mode) declared with `[[revet::toggle]]` and stored in cookies. Codegen generates the toggle route and the `urls::` entry.

```cpp
// app/states/ui.hpp
// The URL string here is the route *definition* — urls::sb_open is generated from it
[[revet::toggle("/toggle_sidebar", true)]]  bool sb_open;
[[revet::toggle("/toggle_theme",   false)]] bool dark_mode;
```

Read in layouts and route handlers:

```cpp
bool open = web::toggles::sb_open(req.cookies);
```

Toggle form (zero JS):

```cpp
html::form{ html::attr.method("POST"), html::attr.action(urls::sb_open),
    web::csrf::field(req),
    html::button{ html::attr.type("submit"), "☰" }
}
```

Toggle via HTMX:

```cpp
html::input{ html::attr.type("checkbox"),
    htmx::attr::post(urls::sb_open),
    htmx::attr::trigger("change"),
    htmx::attr::swap("none") }
```

### i18n

```csv
# locale/strings.csv
key,en,fr
page_title,Home,Accueil
nav_about,About,À propos
```

```cpp
html::h1{ i18n::page_title }
html::a{ i18n::nav_about }

// Set locale in middleware
web::use([](const web::Request& req, const web::Next& next) {
    auto lang = req.cookies.get("locale");
    i18n::set_locale(lang.empty() ? "en" : lang);
    return next();
});
```

### HTMX

[HTMX](https://htmx.org) (by Carson Gross) is a lightweight library that extends HTML with `hx-*` attributes for server-driven partial updates, out-of-band swaps, SSE, and more — without writing JavaScript. revet has first-class support: `htmx::attr::` keeps all attribute values type-safe (using `urls::` constants instead of raw strings), CSRF tokens inject automatically via `web::csrf::htmx_headers`, and optimistic update deltas are checked at compile time.

```cpp
#include <revet/htmx.hpp>

html::button{
    htmx::attr::post(urls::toggle_sidebar),
    htmx::attr::target("#sidebar"),
    htmx::attr::swap("outerHTML"),
    "Toggle"
}

// Detect HTMX request — return fragment instead of full layout
if (htmx::is_request(req)) {
    return web::to_response(partial_html);
}
return layout_main(req, "Title", full_page);
```

Response helpers: `htmx::redirect`, `htmx::retarget`, `htmx::reswap`, `htmx::trigger`, `htmx::refresh`, `htmx::push_url`, `htmx::replace_url`.

#### Optimistic Updates

`htmx::optimistic(id, delta)` fires client-side immediately before the HTMX request. When the server fragment arrives it swaps in via `outerHTML`, confirming or correcting the optimistic value.

Use `html::typed_id<T>` instead of `html::id` when the element's text content has a known C++ type. `htmx::optimistic` is overloaded on it — the compiler enforces the delta type.

```cpp
static constexpr html::typed_id<int> k_counter{"counter-value"};

// GET: display counter with optimistic form
html::div{ k_counter, styles::counter_value, val },
html::form{
    html::attr.action(urls::index),
    htmx::attr::post(urls::index),
    htmx::attr::target(k_counter),
    htmx::attr::swap_outer_html(),
    htmx::optimistic(k_counter, +1),     // type-checked int delta
    web::csrf::field(req),
    html::submit{ styles::btn, "Increment" }
}

// POST: fragment for HTMX, redirect for plain form
static auto render_post(const web::Request& req, web::State<Counter> counter) {
    int next = (counter ? counter->count : 0) + 1;
    if (htmx::is_request(req))
        return web::to_response(html::div{ k_counter, styles::counter_value, next }).state(Counter{next});
    return web::redirect(urls::index).state(Counter{next});
}
```

Overload summary:

```cpp
static constexpr html::typed_id<int>         k_count{"count"};
htmx::optimistic(k_count, +1)             // int element — type-checked delta

static constexpr html::typed_id<std::string> k_label{"status"};
htmx::optimistic(k_label, "Saving...")   // string element — replace text

static constexpr html::id                    k_btn{"submit-btn"};
htmx::optimistic(k_btn, "e.classList.toggle('active')")  // arbitrary JS
```

### JSON API Routes

revet supports JSON API endpoints alongside server-rendered HTML routes. The system reuses file-based routing, `[[revet::model]]` codegen, route guards, and HTTP status codes.

#### Directory convention

```
app/routes/api/users.cpp          → GET/POST  /api/users
app/routes/api/users/[id].cpp     → GET/PUT/DELETE /api/users/:id
app/routes/api/v1/users.cpp       → GET/POST  /api/v1/users
```

Versioning is free via nested directories. All versions are registered simultaneously.

#### Handler styles

**Full-control** (`web::Request` → `web::Response`):

```cpp
static auto render_get(const web::Request& req) -> web::Response {
    return { .body = revet::json::to_json<UserList>(list),
             .content_type = "application/json" };
}
```

**Typed-param** (model param → model return) — the framework handles deserialization, validation, and serialization:

```cpp
// Never called if body is missing, malformed, or required fields absent
static auto render_post(const CreateUser& body) -> User {
    return User{ .id = new_id(), .name = body.name, .email = body.email };
}
```

The generated wrapper: checks `Content-Type: application/json`, rejects empty body, parses JSON, calls handler, serializes return value to `application/json`.

Array bodies are supported: use `std::vector<Model>` as param or return type.

`std::optional<T>` fields: absent or `null` in requests → `std::nullopt`; `std::nullopt` in responses → `"field":null`. Extra fields in requests are silently ignored.

API route handlers may be **sync or async**. Return `T` / `std::vector<T>` / `web::Response` for sync handlers, or `web::Task<T>` / `web::Task<std::vector<T>>` / `web::Task<web::Response>` to `co_await` outbound HTTP, DB queries, or other coroutine APIs before serializing the JSON response.

```cpp
// app/routes/api/v1/users.cpp — async API handler
static auto render_get(const web::Request&) -> web::Task<std::vector<User>> {
    const auto rows = co_await db::query<User>("SELECT * FROM users");
    co_return rows;
}
```

#### Error handling

| Condition | Status | Body |
|---|---|---|
| Wrong `Content-Type` | 400 | `{"error":"Content-Type: application/json required"}` |
| Empty body | 400 | `{"error":"request body required"}` |
| Malformed JSON | 400 | `{"error":"JSON parse error"}` |
| Required field missing | 400 | `{"error":"Field not found: name"}` |

For full-control handlers:

```cpp
return web::api::error(web::status::not_found, "user not found");
// → {"error":"user not found"} with 404
```

#### Authentication

API routes do not get CSRF protection — they use token-based auth instead. Use `api_guards()` (not `route_guards()`) to tell codegen not to inject CSRF middleware.

**Fixed-token bearer guard** (constant-time):

```cpp
static auto api_guards() -> std::vector<web::Middleware> {
    return { web::api::bearer_static(env::API_KEY) };
}
```

`bearer_static` compares via `web::crypto::secure_eq` so the token can't be recovered byte-by-byte through response-timing analysis. An empty `expected` (e.g. missing env var) fails closed — every request is rejected.

**Custom verify** (when you need dynamic lookup):

```cpp
static auto api_guards() -> std::vector<web::Middleware> {
    return { web::api::bearer([](std::string_view tok) -> bool {
        return web::crypto::secure_eq(tok, env::API_KEY);   // NEVER `tok == ...`
    })};
}
```

**Typed bearer guard** (validate + resolve principal):

```cpp
struct ApiKey { std::string user_id; std::vector<std::string> scopes; };

static auto api_guards() -> std::vector<web::Middleware> {
    return { web::api::bearer<ApiKey>([](std::string_view tok) -> std::optional<ApiKey> {
        return db::find_api_key(tok);  // std::nullopt → 401
    })};
}

static auto render_get(const web::Request& req) -> web::Response {
    auto& key = web::api::principal<ApiKey>();
    // key.user_id, key.scopes — guaranteed present if guard passed
}
```

`web::api::bearer()` reads `Authorization: Bearer <token>` and returns 401 `{"error":"missing token"}` or `{"error":"invalid token"}` on failure. The handler never sees the raw token.

#### Reading raw request body

```cpp
static auto render_post(const web::Request& req) -> web::Response {
    if (req.raw_body.empty())
        return web::api::error(web::status::bad_request, "body required");
    try {
        revet::json::Document doc(req.raw_body);
        auto name = static_cast<std::string>(doc["name"]);
    } catch (const revet::json::ParseError& e) {
        return web::api::error(web::status::bad_request, e.what());
    }
}
```

`req.raw_body` is populated when `Content-Type: application/json`.

#### CORS

```cpp
resp.headers.data["Access-Control-Allow-Origin"] = "*";
```

For a CORS preflight guard, add it to `api_guards()`.

### Outbound HTTP API Calls

Type-safe outbound HTTP calls are declared in `app/apis/*.hpp` using the `[[revet::external]]` attribute. Codegen reads the headers and emits inline coroutine definitions in `apis.hpp` — same idea as `[[revet::model]]`, just for HTTP endpoints.

#### Declaration

```cpp
// app/apis/usgs.hpp
#pragma once
#include <revet/web.hpp>

namespace usgs_gov {

struct [[revet::model]] CountResponse {
    uint32_t count;
    uint32_t maxAllowed;
};

[[revet::external("GET https://earthquake.usgs.gov/fdsnws/event/1/count?format=geojson")]]
auto count(std::string starttime, std::string endtime) -> web::Task<CountResponse>;

}  // namespace usgs_gov
```

- **Response struct** — Marked `[[revet::model]]` so it gets `from_json` for free.
- **Forward declaration** — The `[[revet::external("METHOD URL")]]` attribute carries the method + URL with `{name}` placeholders.
- **Params** — Whichever names appear as `{name}` in the URL are path-substituted. Remaining primitives are appended as query-string params. Defaults work natively (`std::string starttime = "2024-01-01"`).
- **Namespace** — Whatever you wrote. No magic domain-derivation.

The template and demo ship a live working example against this same USGS endpoint — see `app/apis/usgs.hpp` and `app/routes/quakes.cpp`.

#### Usage

```cpp
static auto render_get(const web::Request& req) -> web::Task<web::Response> {
    const auto data = co_await usgs_gov::count("2024-01-01", "2024-01-02");
    co_return layout_main(req, "Quakes", html::p{ "count: ", data.count });
}
```

> Outbound HTTP coroutines work in both regular routes (`web::Task<web::Response>`) and `/api/*` handlers (`web::Task<T>`) — see [JSON API Routes](#json-api-routes).

#### Errors

Generated API functions throw `http::Error` on any non-2xx response or transport failure (DNS, connect, timeout, TLS, etc.):

```cpp
try {
    const auto data = co_await usgs_gov::count(start, end);
    co_return layout_main(req, "Quakes", html::p{ "count: ", data.count });
} catch (const http::Error& e) {
    // e.status_code: HTTP status (e.g. 404, 503); 0 means transport failure
    // e.body:        raw response body (or curl error message if transport)
    co_return web::Response{ .status = 502, .body = "upstream failed" };
}
```

If you don't catch, the exception propagates out of the route and the framework returns 500 to the original request — fine for prototyping, not for production.

Defaults: 10 s connect timeout, 30 s total request timeout. The low-level `http::get(url)` / `http::post(url)` etc. **never throw** — they return `http::Response` with `status_code == 0` on transport failure, so callers who want manual handling can opt out of the generated wrapper.

#### Transforms

There's no special transform syntax — just write a wrapper:

```cpp
namespace usgs_gov {
    struct Daily { uint32_t day; uint32_t total; };

    inline auto daily(std::string date) -> web::Task<Daily> {
        auto raw = co_await count(date, date);
        co_return Daily{ /* parse date */, raw.count };
    }
}
```

### Error Handlers

Register in `site_init.cpp`. Called automatically on matching status.

```cpp
web::on_error(web::status::not_found, [](const web::Request& req) {
    return layout_main(req, "Not Found", html::h1{ "404 — Not found" });
});
web::on_error(web::status::internal_error, [](const web::Request& req) {
    return layout_main(req, "Error", html::h1{ "Something went wrong" });
});
```

Handled automatically: 404, 405, 413, 500.

### Static File Serving

```cpp
// site_init.cpp — set once
web::set_public_root(std::filesystem::current_path() / "public");

// In a route handler
return web::serve_file(req.params.get("file"));   // MIME auto-detected from extension
return web::serve_file("logo.png", "image/png");  // explicit MIME
```

Supported auto-detection: html, css, js, json, svg, png, jpg, gif, ico, woff, woff2, ttf, otf, pdf, txt.

### Logging

```cpp
#include <revet/log.hpp>

static const log::TuTag LOG(log::tags::ROUTES);  // file-scope tag, once per TU

log::info("user created: {}", name);
log::warn("missing field: {}", field);
log::error("db failure: {}", msg);
log::debug("params: {}", req.params.get("id"));
```

Available tags: `log::tags::ROUTES`, `log::tags::DB`, `log::tags::APP`, `log::tags::FRAMEWORK`.

The variable must not be named `_log` — identifiers starting with `_` are reserved in the global namespace (`bugprone-reserved-identifier`). `LOG` is the convention.

Explicit tag override (requires at least one format argument):

```cpp
log::info<log::tags::DB>("query: {}", sql);
```

Configure in `revet.toml`:

```toml
[logging]
level = "info"  # debug | info | warn | error
```

## Zero Raw Strings

Every string in a revet route handler or layout must come from a type-safe facility — no raw path strings, raw status integers, raw class names, or raw HTML.

| System | Avoid | Use instead |
|--------|-------|-------------|
| HTML | `"<div class=\"card\">"` | `html::div{styles::card, ...}` |
| SVG | `html::raw("<svg>...</svg>")` | `svg::svg{svg::attr.viewBox(...), ...}` |
| CSS class | `html::attr.class_("btn sm")` | `styles::btn, styles::sm` |
| URL | `"/users/new"` | `urls::users_new` |
| Parameterized URL | `"/users/" + id` | `urls::users__id_(id)` |
| HTTP status | `.status = 404` | `.status = web::status::not_found` |
| DB query (async) | `db.query("SELECT ...")` | `co_await queries::users::all(db)` |
| DB query (sync) | `db.query("SELECT ...")` | `queries::users::sync_count(db)` |
| Outbound HTTP | `http::get("https://...")` | `co_await usgs_gov::count(start, end)` |
| i18n string | `"Contact Us"` | `i18n::page_contact_h1` |
| Log tag name | `static log::TuTag _log(...)` | `static log::TuTag LOG(...)` |
| CSRF token | `<input name="_csrf" ...>` | `web::csrf::field(req)` |
| Flash/state | manual cookie serialize | `web::redirect(url).flash(Toast{...})` |
| Toggle route | `htmx::post("/toggle_sidebar")` | `htmx::post(urls::sb_open)` |

The one exception: `[[revet::toggle("/some-route", default)]]` in `app/states/` contains a raw URL string — this is the *definition* of the route, not a call site. `urls::some_route` is generated from it. `html::raw(s)` is similarly reserved for framework internals; it bypasses XSS escaping and must never appear in user code.

HTTP status code values: `web::status::ok`, `web::status::not_found`, `web::status::unauthorized`, `web::status::forbidden`, `web::status::method_not_allowed`, `web::status::bad_request`, `web::status::internal_server_error`, `web::status::no_content`, `web::status::found`.

## Async Model

Two thread pools:

| Pool | Size | Handles |
|---|---|---|
| Request pool | `hardware_concurrency()` | Sync handlers, request parsing |
| IO pool | `4 × hardware_concurrency()` | DB/HTTP calls, coroutine continuations |

```cpp
// Sync — blocks request thread until done
static auto render_get(const web::Request& req) -> web::Response { ... }

// Async — request thread freed immediately; continuation runs on IO pool
static auto render_get(const web::Request& req) -> web::Task<web::Response> {
    auto rows = co_await queries::users::all(db);
    co_return layout_main(req, "Users", ...);
}

// Offload any blocking work from a coroutine
auto result = co_await web::run_on_io([&] { return blocking_call(); });
```

SSE routes run in a detached thread — not tied to either pool — so long-lived connections don't exhaust pool slots.

Codegen detects `-> web::Task<web::Response>` and registers the route as `async_get` instead of `get`.

### Thread-local context

`web::Context` is thread-local and cleared at the start of each request.

```cpp
auto& ctx = web::Context::current();
ctx.files;   // uploaded files (after uploads::allow moves them to validated_dir)
ctx.states;  // type-erased map backing State<T> / Flash<T>
```

## Development Workflow

### Dev server

```bash
revet dev [--port 3000] [--asan] [--lldb] [--profile]
```

- File watcher rebuilds on changes to `app/`, `styles/`, `cmake/`, `revet.toml`
- Press `r` to rebuild, `t` to see routes, `d` to attach debugger, `q` to quit

### Code quality

```bash
revet tidy      # clang-tidy with safety fixes
revet format    # clang-format
revet clean     # remove build artifacts
```

### Testing

Tests use Catch2. Place test files in `tests/`:

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Basic test") {
    CHECK(1 + 1 == 2);
}
```

Run via `revet build` (if Catch2 available) or the framework test suite (`./scripts/revet-fw test`).

## Quick Reference

| What | Pattern |
|------|---------|
| HTML element | `html::div{ styles::card, html::p{ "text" } }` |
| CSS class | `styles::btn` (never raw strings) |
| Static URL | `urls::users_new` |
| Parameterized URL | `urls::users__id_(id)` |
| HTTP status | `web::status::not_found` |
| DB query (async) | `co_await queries::users::all(db)` |
| DB query (sync) | `queries::users::sync_count(db)` |
| Offload to IO pool | `co_await web::run_on_io([&] { return blocking(); })` |
| Redirect | `web::redirect(urls::home)` |
| Redirect back | `web::back(req)` |
| Set flash | `web::redirect(url).flash(Toast{ .message = "ok" })` |
| Set session | `web::redirect(url).state(Session{ .user_id = id })` |
| CSRF field | `web::csrf::field(req)` |
| Log | `log::info("msg: {}", val)` |
| i18n string | `i18n::page_title` |
| Export component | `[[revet::export]] html::Node btn(...) { ... }` |
| Serve static file | `web::serve_file(path)` |
| Error handler | `web::on_error(web::status::not_found, handler)` |
| Thread-local context | `web::Context::current()` |
| API error response | `web::api::error(web::status::not_found, "msg")` |

## Framework Development

### Framework maintainers

```bash
cd ~/revet

./scripts/revet-fw test    # run framework tests with ASan
sudo ./scripts/revet-fw install  # deploy to ~/.revet/current/
./scripts/revet-fw tidy    # lint with fixes
./scripts/revet-fw format  # format code
./scripts/revet-fw clean   # remove build artifacts
python3 scripts/check_deps.py    # check dependencies
```

### Framework source structure

```
revet/
├── include/revet/       ← Framework public headers
├── src/                 ← Framework implementation
├── cmake/               ← Code generation scripts (gen_routes.py, etc.)
├── template/            ← User project template (scaffolded by `revet new`)
├── demo/                ← Full-featured demo app
├── scripts/
│   ├── revet-fw         ← Framework developer tool
│   ├── check_deps.py    ← Dependency validator
│   └── install_deps.sh  ← Auto-installer
├── bin/revet            ← User-facing CLI
├── conanfile.py         ← Framework dependency manifest
└── tests/               ← Framework unit tests
```

### Codegen pipeline

| Script | Input | Output |
|---|---|---|
| `gen_routes.py` | `app/routes/**/*.cpp` | `routes.hpp`, `routes.cpp` |
| `gen_resources.py` | `revet.toml` | `resources.hpp` |
| `gen_queries.py` | `app/queries/*.sql` | `queries.hpp` |
| `gen_styles.py` | `app/styles/*.css` | `styles.hpp` |
| `gen_components.py` | `app/components/*.cpp`, `app/layouts/*.cpp` | `components.hpp` |
| `gen_json_models.py` | `models/*.hpp` | `models.hpp` |
| `gen_states.py` | `app/states/*.hpp` | `states.hpp` |
| `gen_toggles.py` | `app/states/*.hpp` | `toggles.hpp` |
| `gen_forms.py` | `app/forms/*.hpp` | `forms.hpp` |
| `gen_api.py` | `app/apis/*.hpp` | `apis.hpp` |
| `gen_i18n.py` | `locale/*.csv` | `i18n.hpp` |

All outputs land in `build/generated/`.

### Dependencies

**Required:** CMake ≥ 3.20, clang++ ≥ 17 (or g++ ≥ 13), Python 3.8+, libsodium

**Optional:** Ninja (faster builds), Catch2 3 (testing), SQLite 3, ccache, clang-tidy, clang-format

## Contributing

1. Fork and clone
2. Install dependencies: `./scripts/install_deps.sh`
3. Create a feature branch: `git checkout -b feature/my-feature`
4. Make changes and run tests: `./scripts/revet-fw test`
5. Ensure code quality: `./scripts/revet-fw tidy && ./scripts/revet-fw format`
6. Submit a pull request

## Acknowledgments

revet stands on the shoulders of these open-source projects. If you ship something on top of revet, you're using all of them — please respect their licenses and credit the upstream where you depend on them directly. The full upstream notice texts are aggregated in [`THIRD_PARTY_LICENSES`](THIRD_PARTY_LICENSES); ship that file with your binary when you redistribute. See [docs/deploy.md](docs/deploy.md#third-party-license-attribution) for the why and how.

**Runtime dependencies** (linked into the binary)

- [libsodium](https://doc.libsodium.org/) — XChaCha20-Poly1305 AEAD for cookie sealing, `sodium_memcmp` for constant-time compares. ISC.
- [libcurl](https://curl.se/libcurl/) — outbound HTTP client. MIT-style (curl).
- [simdjson](https://simdjson.org/) — SIMD-accelerated JSON parsing. Apache-2.0.
- [CTRE](https://github.com/hanickadot/compile-time-regular-expressions) — compile-time regular expressions. Apache-2.0.
- [SQLite](https://www.sqlite.org/) — embedded SQL database. Public domain.
- [libpq / PostgreSQL](https://www.postgresql.org/docs/current/libpq.html) — Postgres client library (optional). PostgreSQL License.

**Vendored into the binary**

- [htmx](https://htmx.org/) by Carson Gross — server-driven UI without writing JavaScript. Zero-clause BSD.

**Build + test tooling**

- [CMake](https://cmake.org/) — build system. BSD-3-Clause.
- [Ninja](https://ninja-build.org/) — build executor (auto-picked when available). Apache-2.0.
- [Conan](https://conan.io/) — C++ package manager. MIT.
- [Catch2](https://github.com/catchorg/Catch2) — C++ test framework. BSL-1.0.
- [LLVM / Clang](https://llvm.org/) — primary supported compiler toolchain. Apache-2.0 with LLVM exceptions.
- [Bun](https://bun.sh/) — TypeScript bundler for `static/optimistic.ts` (optional). MIT.

## License

MIT
