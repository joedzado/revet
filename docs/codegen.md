# Codegen

Codegen turns `app/` and `revet.toml` into C++ headers in `build/generated/`. It runs at build time through `revet_codegen_tool`, built from `cmake/codegen/`.

- Invocation: `revet_codegen_tool <generator> <project-root> <framework-root>`
- `main.cpp` dispatches by generator name; `gen_*.cpp` holds each generator; `common.*` and `node.*` are shared helpers.
- CMake wiring: `template/CMakeLists.txt`, `Codegen & Build` block.
- The tool is built once with the framework and installed to `<framework>/prebuilt/bin` (`install.sh`). App builds run that binary. With no prebuilt tool (framework used from a source checkout) the app's configure step compiles it once.
- `<framework>/prebuilt/lib` holds the vendored static libs (libsodium, simdjson), so app builds compile neither.
- Patterns are CTRE, checked at compile time. In CTRE `.` matches newlines; use `[^\n]` to stay on one line.

## Generators

| Generator | Inputs | Outputs | Purpose |
|---|---|---|---|
| `gen_json_models` | `app/**/*.hpp` | `models.hpp` | `from_json`/`to_json` for `[[revet::model]]` structs |
| `gen_states` | `app/states/*.hpp` | `states.hpp` | Cookie codecs for `web::State<T>` / `web::Flash<T>` |
| `gen_forms` | `app/forms/*.hpp` | `forms.hpp` | Render helpers and POST decoders for `[[revet::form]]` |
| `gen_components` | `app/components/*.cpp`, `app/layouts/*.cpp` | `components.hpp` | Declarations for `[[revet::export]]` functions |
| `gen_routes` | `app/routes/**/*.cpp` | `routes.cpp`, `routes.hpp` | Routes from file paths; sync/async and SSE detection; `urls::`; patches `build/compile_commands.json` |
| `gen_resources` | `revet.toml [resources.*]` | `resources.hpp`, `db_backends.cmake` | Static SQLite/Postgres pool per resource |
| `gen_queries` | `app/queries/*.sql` | `queries.hpp` | Typed `queries::<file>::<name>()` from `-- @revet:name:` blocks |
| `gen_api` | `app/apis/*.hpp` | `apis.hpp` | Coroutines for `[[revet::external("METHOD URL")]]` |
| `gen_styles` | `app/styles/*.css` | `styles.hpp` | Class and id constants; embedded CSS |
| `gen_toggles` | `app/states/*.hpp` | `toggles.hpp` | Flip routes and readers for `[[revet::toggle]]` |
| `gen_uploads` | `revet.toml [uploads.*]` | `uploads.hpp` | `uploads::<bucket>::allow(req, validator)` |
| `gen_dependencies` | `revet.toml [build.dependencies]` | `dependencies.cmake` | `target_link_libraries` for Conan deps; also run at configure time |
| `gen_server` | `revet.toml` top-level `max_body`, `data_dir` | `server.hpp` | Sets `web::config` values at static init; env and flags still override |
| `gen_scripts` | `app/scripts/*.js` | `scripts.hpp` | Embeds each script with a content-hashed URL; run by `gen_routes`, which serves it `immutable` |
| `gen_i18n` | `app/locale/*.csv` | `i18n.hpp` | `i18n::<key>` strings per locale |
| `gen_htmx` | `static/htmx.min.js`, `revet.toml [htmx].version` | `htmx_asset.hpp` | Bundled htmx as a `string_view`, served at `/_revet/htmx.js` |
| `gen_optimistic` | `static/optimistic.ts` | `optimistic_asset.hpp` | Optimistic-update runtime, compiled with `bun`; skipped if `bun` is missing |

`gen_htmx` and `gen_optimistic` live in `gen_assets.cpp`.

## Ordering

- `gen_routes` needs `apis.hpp` from `gen_api`.
- `gen_json_models` runs before `gen_api`; `apis.hpp` includes `models.hpp`.

## Adding a generator

1. Add `cmake/codegen/gen_<name>.cpp` with `int gen_<name>(const revet::codegen::Ctx&)`. Write output with the `common.hpp` helpers. Throw `GenError` for user errors.
2. Register it in `generators()` in `main.cpp`.
3. Add an `add_codegen(Name gen_<name> "${GEN_DIR}/<name>.hpp")` line in `template/CMakeLists.txt` and list the output in `GENERATED_SOURCES`.
4. Cover it in `tests/codegen/fixture`, then refresh the expected output (below).

## Tests

`ctest -R codegen` runs every generator on `tests/codegen/fixture` and compares the result with `tests/codegen/expected`. It also checks that each input in `tests/codegen/errors/` fails with the message in its `expect.txt`.

After an intended output change, rewrite the expected files and review the diff:

```sh
cmake -DTOOL=build/tools/revet_codegen_tool -DFRAMEWORK=$PWD -DWORK=/tmp/codegen_test -DUPDATE=1 -P tests/codegen/run.cmake
```
