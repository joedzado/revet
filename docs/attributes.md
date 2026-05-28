# Attribute reference

revet uses C++23 attributes as markers for the codegen pipeline. The compiler silently ignores unknown attributes (warning suppressed via `-Wno-unknown-attributes`); the Python scanners pick them up at configure time.

## `[[revet::model]]`

```cpp
// app/models/user.hpp
struct [[revet::model]] User {
    uint32_t    id;
    std::string name;
    std::string email;
    std::optional<std::string> phone;
};
```

**Scanned by:** `gen_json_models.py` (anywhere under `app/**` plus `app/apis/*.hpp`).

**Emits:** `revet::json::from_json<User>(Document&)` and `to_json<User>(const User&)` template specializations into `models.hpp`. Honors `std::optional` (absent or `null` → `nullopt`), `std::vector<T>` at field positions, nested `[[revet::model]]` types.

## `[[revet::external("METHOD URL")]]`

```cpp
// app/apis/usgs.hpp
namespace usgs_gov {
    struct [[revet::model]] CountResponse { uint32_t count; uint32_t maxAllowed; };
    [[revet::external("GET https://earthquake.usgs.gov/fdsnws/event/1/count?format=geojson")]]
    auto count(std::string starttime, std::string endtime) -> web::Task<CountResponse>;
}
```

**Scanned by:** `gen_api.py` (only `app/apis/*.hpp`).

**Emits:** an inline coroutine definition into `apis.hpp` that:

1. Builds the URL — substitutes `{name}` path placeholders with matching params, appends remaining params as query string.
2. `co_await`s `http::<method>(url)`.
3. Throws `http::Error(status, body)` if `!resp.ok()`.
4. Parses `resp.body` with `revet::json::from_json<Return>`.

The function declaration in the header is what `gen_api` reads; the codegen produces the *definition*. You can call the function from any code that includes `apis.hpp` (auto-included by `gen_routes`).

## `[[revet::toggle]]`

```cpp
// app/states/dark_mode.hpp
struct [[revet::toggle]] DarkMode { bool enabled = false; };
```

**Scanned by:** `gen_toggles.py` (`app/states/*.hpp`).

**Emits:** into `toggles.hpp`:

- A POST route to flip the toggle (`urls::toggle_dark_mode`).
- A reader `web::toggles::dark_mode(req.cookies)` that returns the current bool.
- A cookie codec keyed off the type name.

## `[[revet::form]]`

```cpp
// app/forms/contact.hpp
struct [[revet::form]] ContactForm {
    std::string name;
    std::string email;
    std::string message;
};
```

**Scanned by:** `gen_forms.py` (`app/forms/*.hpp`).

**Emits:** form helpers into `forms.hpp` — typed `decode<ContactForm>(req)`, render utilities, etc. Used in conjunction with `app/routes/<x>.cpp` that has a `render_post(const ContactForm&)`.

## `[[revet::export]]`

```cpp
// app/components/ui.cpp
[[revet::export]] auto button(std::string_view label) -> html::Node { ... }
```

**Scanned by:** `gen_components.py` (`app/components/*.cpp`, `app/layouts/*.cpp`).

**Emits:** a forward declaration into `components.hpp` so any route file can call the function without including the source. Resolves at link time.

## Caveats

- The scanners are **regex-based**, not AST-based. They handle most idiomatic C++ but will trip over heavy macros, deeply nested templates with `<>`-shaped contents at unfortunate positions, or attributes spread across lines via macro expansion. If a scanner doesn't pick up your declaration, simplify the surface or file a bug.
- Attributes go **on the declaration**, not the definition. For `[[revet::external]]`, the scanned text is the function declaration in the header; the codegen synthesizes the definition.
- Attribute typos are silent — `[[revet::modle]]` will compile fine and produce nothing. Run the codegen output (`build/generated/`) through eyeballs the first time you add a new attribute.

## See also

- [codegen.md](./codegen.md) for which script reads which attribute and what each emits.
- [contributing.md → Adding a `[[revet::*]]` attribute](contributing.md#adding-a-revet-attribute).
