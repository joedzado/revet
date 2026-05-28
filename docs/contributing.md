# Contributing

How to set up, test, and ship changes against the framework itself (not an app built on it).

## Setup

```bash
git clone https://github.com/joedzado/revet ~/revet
cd ~/revet
bash scripts/install_deps.sh        # libsodium, libcurl, libsqlite, libpq, conan, python3, cmake
sudo scripts/revet-fw install       # populate ~/.revet/current and install /usr/local/bin/revet
```

Verify:

```bash
revet --version          # → 0.1.5 (or whatever VERSION says)
scripts/revet-fw test    # full suite — should be all green
```

## Dev loop

Edits to framework source (`include/revet/`, `src/`, `cmake/gen_*.py`, etc.) do **not** appear in `revet dev` of a test app until you re-run:

```bash
sudo scripts/revet-fw install
```

That copies the source tree to `~/.revet/current/`. There is no live-reload across this boundary.

If you're iterating very heavily on framework code, point a test app's `CMakeLists.txt` `FRAMEWORK_DIR` at your source repo directly. Don't commit that change.

## Tests

```bash
scripts/revet-fw clean             # blow away build dir
scripts/revet-fw test              # run both C++ (catch2) and Python (unittest) suites
```

Run a single C++ test:

```bash
ctest --test-dir build --output-on-failure -R test_csrf
# or run the binary directly:
build/test_csrf
```

Run a single Python test:

```bash
python3 -m unittest tests.test_gen_api.TestExternalAttributeParsing -v
```

ASan + UBSan build:

```bash
scripts/revet-fw clean
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DREVET_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Conventions

There's no formal style guide — the `.clang-format` and `.clang-tidy` files are the source of truth. Highlights:

- C++23. Use `co_await` over manual continuations where possible.
- 120-column line limit. Pointer alignment left (`int* x`).
- `snake_case` for variables and functions. `PascalCase` for types. `k_` prefix for compile-time constants.
- Prefer header-only when it's plausible — framework's surface is mostly in `include/revet/*.hpp`. Move to `src/` only when you'd otherwise drag a heavy library into every TU.
- Throw on hard errors at framework boundaries (e.g. `crypto::get_key` on a malformed key, `http::Error` on bad response). Don't return error codes.
- Don't write comments that restate the code. Do write a one-line comment when a constraint or non-obvious invariant lives in the next few lines.

For new framework features, add tests in `tests/` (C++) or `tests/test_*.py` (codegen). The bar for merging: full suite still green, new feature has at least one happy-path test and one sad-path test.

## Adding a codegen script

See [codegen.md → "Adding a new codegen script"](./codegen.md#adding-a-new-codegen-script).

## Adding a `[[revet::*]]` attribute

The existing attributes (`model`, `external`, `toggle`, `form`, `export`) follow a common pattern:

1. **Decide what to scan and where to scan it.** `[[revet::model]]` is per-struct, scanned across `app/**/*.hpp`. `[[revet::external]]` is per-function-declaration, scanned in `app/apis/*.hpp` only. Pick the scope your attribute needs.
2. **Write a parser script** in `cmake/gen_<thing>.py`. Most existing parsers are regex-based — fine for the surface we control. Use `cmake/codegen.py` helpers (namespace tracking, write-if-changed) so your script behaves like the others.
3. **Emit a header** users can `#include`. Don't emit code into the user's TU directly; emit into `build/generated/<thing>.hpp` and `#include` from somewhere they already include (often the auto-included `routes.hpp` or `models.hpp`).
4. **Wire into CMake** — see [codegen.md](./codegen.md#adding-a-new-codegen-script).
5. **Add tests** — both the Python codegen test (`tests/test_gen_<thing>.py`) and a C++ behavior test that exercises the generated output against the runtime.
6. **Document it** in the README's relevant section and add a row in `codegen.md`.

## Release process

There isn't one yet. Current state:

- `VERSION` is the single source of truth.
- `bin/revet` and `conanfile.py` both read it at runtime.
- No git tags. No CHANGELOG. No CI.

Planned shape (not done yet):

- Bump `VERSION` → commit → `git tag v$(cat VERSION)` → push tags.
- GitHub Actions builds the framework on macOS + Ubuntu against the tag.
- A release tarball is attached to the GitHub release, checksummed.
- `install.sh --version vX.Y.Z` pins to that release.

Until that's wired, version bumps don't ship anywhere except this repo.

## Code review

There's a `revet/ultrareview` workflow for multi-agent code review on PRs:

```
/ultrareview            # review the current branch
/ultrareview <PR#>      # review a GitHub PR
```

Not a substitute for human review on anything security-sensitive.

## Reporting bugs

GitHub issues — include `revet --version`, your OS, the reproducer (project layout if relevant), and the exact failing command and output.
