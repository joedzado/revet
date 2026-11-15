#pragma once
// Shared helpers for the revet-codegen tool .
// Everything here is std-only so the tool builds with no Conan dependencies.

#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace revet::codegen {

namespace fs = std::filesystem;
using Lines = std::vector<std::string>;
using Vars = std::vector<std::pair<std::string, std::string>>;
using Re = std::regex;
using Match = std::smatch;

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

// ── Regex helpers ───────────────────────────────────────────────────────────
// Anchored search at the start of `s` .
bool match_prefix(const std::string& s, Match& m, const Re& re);
// Full-string match .
bool match_full(const std::string& s, Match& m, const Re& re);

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
