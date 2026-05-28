# Upgrading

How to move a project to a newer framework version.

## Framework upgrade (no breaking changes)

```bash
curl -fsSL https://raw.githubusercontent.com/joedzado/revet/main/install.sh | sh
# in your project:
revet upgrade           # regenerates per-project CMakeLists from the new template
revet clean
revet dev
```

`revet upgrade` syncs your project's `CMakeLists.txt` (which is a generated file, marked as such at the top) against the new template. Your `app/`, `revet.toml`, and `src/` are untouched.

## Breaking changes

Breaking changes between framework versions are listed in the CHANGELOG (when a CHANGELOG exists). Generally:

- **Codegen output changes** are caught at compile time — a renamed function in a generated header surfaces as an "undeclared identifier" error in your code. Fix the call site; recompile.
- **Runtime contract changes** (e.g. cookie format, route registration shape) are caught either at startup (`std::runtime_error` on init) or by the first failing request. Read the release notes for migration steps.

## Cookie key behavior across upgrades

The framework uses `REVET_COOKIE_KEY` for AEAD on state cookies and the CSRF cookie. Upgrades **do not** rotate this key — old cookies remain valid. The same key produces the same ciphertext format across framework versions.

If a future version changes the cookie *format* (not the key), in-flight sessions / CSRF tokens will be invalidated. Plan for that like any logout-all event.

## Database schema

revet has no built-in migration runner. Your schema lives in `app/queries/*.sql` with `-- @revet:name: create_schema` (or similar) markers, executed once at app boot in `app/routes/site_init.cpp`. It is idempotent (`CREATE TABLE IF NOT EXISTS`), not versioned.

For real schema migrations across deploys, run a separate tool (`goose`, `dbmate`, or hand-rolled `psql -f migrations/N.sql`) **before** starting the new binary. The framework will not catch a binary/schema mismatch — it will just throw at first query that hits the wrong column.

## Project dependencies (Conan)

`revet upgrade` doesn't change `revet.toml [build.dependencies]`. To bump a dep:

```bash
revet add somepkg@newversion       # also pins in revet.lock
revet clean && revet dev
```

`revet.lock` should be committed alongside `revet.toml` so the team and CI install the exact same versions.

## Downgrading

Today the install script tracks `main` — there are no released versions to pin to (see [contributing.md → Release process](./contributing.md#release-process)). Downgrade is "git checkout an older revision of the framework, re-run `scripts/revet-fw install`."

When tagged releases exist, `install.sh --version vX.Y.Z` will be the supported path.
