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
#include <ranges>
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
    fs::path proj;      // project root (CMAKE_SOURCE_DIR)
    fs::path framework; // framework root (FRAMEWORK_DIR); templates in cmake/templates
};

// ── String building ─────────────────────────────────────────────────────────
template<class... T>
auto cat(const T&... parts) -> std::string {
    std::string out;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    (out.append(std::string_view(parts)), ...);
    return out;
}

// ── Files ───────────────────────────────────────────────────────────────────
// Reads a file with universal newlines.
auto read_text(const fs::path& p) -> std::string;
auto file_exists(const fs::path& p) -> bool;
// Writes only when the content changed (write-if-changed).
void write_file(const fs::path& p, std::string_view content);
// Lines joined with "\n" and no trailing newline.
void write_lines(const fs::path& p, const Lines& lines);
// build/generated/<filename> under the project; creates the directory.
auto setup(const Ctx& ctx, std::string_view filename) -> fs::path;
// Sorted directory listing for `dir/*<suffix>` (or recursive `**/*<suffix>`).
auto glob(const fs::path& dir, std::string_view suffix, bool recursive = false) -> std::vector<fs::path>;
auto has_models(const fs::path& dir) -> bool;

// ── Templates (framework cmake/templates) ───────────────────────────────────
// Template file contents with trailing newlines stripped.
auto tmpl(const Ctx& ctx, std::string_view name) -> std::string;
// Raw template file contents.
auto tmpl_whole(const Ctx& ctx, std::string_view name) -> std::string;
// string.Template.substitute: $name, ${name}, $$. Missing keys are errors.
auto subst(std::string_view text, const Vars& vars) -> std::string;

// ── Strings ─────────────────────────────────────────────────────────────────
auto strip(std::string_view s) -> std::string;
auto lower(std::string_view s) -> std::string;
auto starts_with(std::string_view s, std::string_view prefix) -> bool;
auto ends_with(std::string_view s, std::string_view suffix) -> bool;
auto join(const Lines& parts, std::string_view sep) -> std::string;
// Splits lines on \n, \r and \r\n.
auto splitlines(std::string_view s) -> Lines;
// Splits on commas that are not nested in <>, (), [] and drops empty items.
auto split_args(std::string_view s) -> Lines;
// Escapes backslash, quote and newline for a C++ string literal.
auto escape_str(std::string_view s) -> std::string;
// Escapes only backslash and double quote.
auto escape_quotes(std::string_view s) -> std::string;
// Collapses runs of whitespace to one space.
auto collapse_ws(std::string_view s) -> std::string;
// Counts newlines before `end`.
auto count_newlines(std::string_view s, std::size_t end) -> std::size_t;
auto rel_include(const fs::path& hpp, const fs::path& proj) -> std::string;
// Header lines: generated-file banner plus includes.
auto file_header(std::initializer_list<std::string> includes) -> Lines;

// ── CTRE helpers ──
// Offset of `part` inside `whole` (part must be a view into whole).
inline auto offset_in(std::string_view whole, std::string_view part) -> std::size_t {
    return static_cast<std::size_t>(part.data() - whole.data());
}
// Replaces every match of P in `s` with `with`.
template<ctll::fixed_string P>
auto replace_all(std::string_view s, std::string_view with) -> std::string {
    std::string out;
    std::size_t pos = 0;
    for (const auto m : ctre::search_all<P>(s)) {
        const std::size_t at = offset_in(s, m.to_view());
        out.append(s.substr(pos, at - pos));
        out += with;
        pos = at + m.size();
    }
    out.append(s.substr(pos));
    return out;
}

// Appends `more` to `out`.
inline void append(Lines& out, const Lines& more) {
    out.insert(out.end(), more.begin(), more.end());
}

// `s` split at every match of P (Python's re.split without groups).
template<ctll::fixed_string P>
auto split_at(std::string_view s) -> Lines {
    Lines pieces;
    std::size_t prev = 0;
    for (const auto m : ctre::search_all<P>(s)) {
        const std::size_t at = offset_in(s, m.to_view());
        pieces.emplace_back(s.substr(prev, at - prev));
        prev = at + m.size();
    }
    pieces.emplace_back(s.substr(prev));
    return pieces;
}

// Capture group N of every match of P in `s`.
template<ctll::fixed_string P, int N = 1>
auto captures(std::string_view s) -> Lines {
    return ctre::search_all<P>(s) |
           std::views::transform([](const auto& m) -> std::string { return m.template get<N>().to_string(); }) |
           std::ranges::to<Lines>();
}

// First place where `name` is directly followed by Tail — a search for name + Tail when
// the name is only known at run time. Captures are Tail's.
template<ctll::fixed_string Tail>
auto search_after(std::string_view src, std::string_view name) {
    for (std::size_t at = src.find(name); at != std::string_view::npos; at = src.find(name, at + 1)) {
        if (auto m = ctre::starts_with<Tail>(src.substr(at + name.size()))) {
            return m;
        }
    }
    return decltype(ctre::starts_with<Tail>(src)){};
}

// Patterns shared by several generators.
// `[[revet::toggle("/route", true|false)]] bool name` — captures: 1 route, 2 default, 3 name.
inline constexpr auto find_toggles = ctre::search_all<
    R"RE(\[\[[\s\S]*?revet::toggle\(\s*"([^"]+)"\s*,\s*(true|false)\s*\)[\s\S]*?\]\]\s*bool\s+(\w+))RE">;
// `web::State<T>` / `web::Flash<T>` — captures: 1 State|Flash, 2 T.
inline constexpr auto find_state_types = ctre::search_all<R"RE(web::(State|Flash)<(\w+)>)RE">;

// ── Generators (one per gen_*.cpp) ─────────────────────────────────────
auto gen_json_models(const Ctx& ctx) -> int;
auto gen_states(const Ctx& ctx) -> int;
auto gen_forms(const Ctx& ctx) -> int;
auto gen_components(const Ctx& ctx) -> int;
auto gen_routes(const Ctx& ctx) -> int;
auto gen_resources(const Ctx& ctx) -> int;
auto gen_queries(const Ctx& ctx) -> int;
auto gen_api(const Ctx& ctx) -> int;
auto gen_styles(const Ctx& ctx) -> int;
auto gen_toggles(const Ctx& ctx) -> int;
auto gen_uploads(const Ctx& ctx) -> int;
auto gen_dependencies(const Ctx& ctx) -> int;
auto gen_i18n(const Ctx& ctx) -> int;
auto gen_htmx(const Ctx& ctx) -> int;
auto gen_optimistic(const Ctx& ctx) -> int;
auto gen_scripts(const Ctx& ctx) -> int;
auto gen_server(const Ctx& ctx) -> int;
// (url, identifier) for each app/scripts/*.js; used by gen_routes.
auto app_script_routes(const Ctx& ctx) -> std::vector<std::pair<std::string, std::string>>;

} // namespace revet::codegen
