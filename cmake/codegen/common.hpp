#pragma once
// Shared helpers for the revet-codegen tool.
// std plus the vendored, header-only CTRE: the tool builds with no Conan dependencies.
//
// Regexes are CTRE (checked at compile time). Unlike std::regex, CTRE's `.` matches
// newlines, so patterns that must stay on one line use [^\n].

#include <cstddef>
#include <ctre.hpp>
#include <filesystem>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace revet::codegen {

namespace fs = std::filesystem;
using Lines = std::vector<std::string>;
using Vars = std::vector<std::pair<std::string, std::string>>;

// User-facing failure. main() prints what() to stderr and exits 1.
struct GenError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Paths passed on the command line: revet-codegen <generator> <project> <framework>.
struct Ctx {
    fs::path proj;       // project root (CMAKE_SOURCE_DIR)
    fs::path framework;  // framework root (FRAMEWORK_DIR); templates in cmake/templates
};

// ── String building ─────────────────────────────────────────────────────────
template <class... T>
std::string cat(const T&... parts) {
    std::string out;
    (out.append(std::string_view(parts)), ...);
    return out;
}

// ── Files ───────────────────────────────────────────────────────────────────
// Reads a file with universal newlines.
std::string read_text(const fs::path& p);
bool file_exists(const fs::path& p);
// Writes only when the content changed (write-if-changed).
void write_file(const fs::path& p, std::string_view content);
// Lines joined with "\n" and no trailing newline.
void write_lines(const fs::path& p, const Lines& lines);
// build/generated/<filename> under the project; creates the directory.
fs::path setup(const Ctx& ctx, std::string_view filename);
// Sorted directory listing for `dir/*<suffix>` (or recursive `**/*<suffix>`).
std::vector<fs::path> glob(const fs::path& dir, std::string_view suffix, bool recursive = false);
bool has_models(const fs::path& dir);

// ── Templates (framework cmake/templates) ───────────────────────────────────
// Template file contents with trailing newlines stripped.
std::string tmpl(const Ctx& ctx, std::string_view name);
// Raw template file contents.
std::string tmpl_whole(const Ctx& ctx, std::string_view name);
// string.Template.substitute: $name, ${name}, $$. Missing keys are errors.
std::string subst(std::string_view text, const Vars& vars);

// ── Strings ─────────────────────────────────────────────────────────────────
std::string strip(std::string_view s);
std::string lower(std::string_view s);
bool starts_with(std::string_view s, std::string_view prefix);
bool ends_with(std::string_view s, std::string_view suffix);
std::string join(const Lines& parts, std::string_view sep);
// Splits lines on \n, \r and \r\n.
Lines splitlines(std::string_view s);
// Splits on commas that are not nested in <>, (), [] and drops empty items.
Lines split_args(std::string_view s);
// Escapes backslash, quote and newline for a C++ string literal.
std::string escape_str(std::string_view s);
// Collapses runs of whitespace to one space.
std::string collapse_ws(std::string_view s);
// Counts newlines before `end`.
std::size_t count_newlines(std::string_view s, std::size_t end);
std::string rel_include(const fs::path& hpp, const fs::path& proj);
// Header lines: generated-file banner plus includes.
Lines file_header(std::initializer_list<std::string> includes);

// ── CTRE helpers ──
// Offset of `part` inside `whole` (part must be a view into whole).
inline std::size_t offset_in(std::string_view whole, std::string_view part) {
    return static_cast<std::size_t>(part.data() - whole.data());
}
// Replaces every match of P in `s` with `with`.
template <ctll::fixed_string P>
std::string replace_all(std::string_view s, std::string_view with) {
    std::string out;
    const char* last = s.data();
    for (auto m : ctre::search_all<P>(s)) {
        std::string_view hit = m.to_view();
        out.append(last, hit.data());
        out += with;
        last = hit.data() + hit.size();
    }
    out.append(last, s.data() + s.size());
    return out;
}

// First place where `name` is directly followed by Tail — a search for name + Tail when
// the name is only known at run time. Captures are Tail's.
template <ctll::fixed_string Tail>
auto search_after(std::string_view src, std::string_view name) {
    for (std::size_t at = src.find(name); at != std::string_view::npos; at = src.find(name, at + 1)) {
        if (auto m = ctre::starts_with<Tail>(src.substr(at + name.size()))) return m;
    }
    return decltype(ctre::starts_with<Tail>(src)){};
}

// Patterns shared by several generators.
// `[[revet::toggle("/route", true|false)]] bool name` — captures: 1 route, 2 default, 3 name.
inline constexpr auto find_toggles =
    ctre::search_all<R"RE(\[\[[\s\S]*?revet::toggle\(\s*"([^"]+)"\s*,\s*(true|false)\s*\)[\s\S]*?\]\]\s*bool\s+(\w+))RE">;
// `web::State<T>` / `web::Flash<T>` — captures: 1 State|Flash, 2 T.
inline constexpr auto find_state_types = ctre::search_all<R"RE(web::(State|Flash)<(\w+)>)RE">;

// ── Generators (one per gen_*.cpp) ─────────────────────────────────────
int gen_json_models(const Ctx& ctx);
int gen_states(const Ctx& ctx);
int gen_forms(const Ctx& ctx);
int gen_components(const Ctx& ctx);
int gen_routes(const Ctx& ctx);
int gen_resources(const Ctx& ctx);
int gen_queries(const Ctx& ctx);
int gen_api(const Ctx& ctx);
int gen_styles(const Ctx& ctx);
int gen_toggles(const Ctx& ctx);
int gen_uploads(const Ctx& ctx);
int gen_dependencies(const Ctx& ctx);
int gen_i18n(const Ctx& ctx);
int gen_htmx(const Ctx& ctx);
int gen_optimistic(const Ctx& ctx);
int gen_scripts(const Ctx& ctx);
int gen_server(const Ctx& ctx);
// (url, identifier) for each app/scripts/*.js; used by gen_routes.
std::vector<std::pair<std::string, std::string>> app_script_routes(const Ctx& ctx);

}  // namespace revet::codegen
