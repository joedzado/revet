#pragma once
// revet CLI: shared declarations. Each area lives in its own .cpp:
//   util.cpp      output, processes, files, hashes, framework location
//   project.cpp   revet.toml, Conan, CMakeLists.txt generation, builds, routes
//   dev.cpp       `revet dev` (file watcher, server process, keys)
//   packages.cpp  `revet packages` (FTXUI package manager)
//   commands.cpp  add, remove, lock, new, upgrade, clean, tidy, format, doctor
//   main.cpp      argument parsing and dispatch

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace revet::cli {

namespace fs = std::filesystem;

// ── Output ──────────────────────────────────────────────────────────────────
inline constexpr std::string_view k_reset = "\033[0m";
inline constexpr std::string_view k_bold_cyan = "\033[1;36m";
inline constexpr std::string_view k_dim = "\033[2m";
inline constexpr std::string_view k_red = "\033[91m";
inline constexpr std::string_view k_green = "\033[92m";
inline constexpr std::string_view k_yellow = "\033[93m";
inline constexpr std::string_view k_cyan = "\033[36m";

void rule(std::string_view s); // ── s ──
void info(std::string_view s);
void err(std::string_view s); // ERROR: s (stderr)
void dim(std::string_view s);
void ok(std::string_view s);                // ✔ s
[[noreturn]] void fail(std::string_view s); // err + exit(1)
auto expand_tabs(std::string_view s, int width = 4) -> std::string;
void clear_screen();

// ── Processes ───────────────────────────────────────────────────────────────
enum class Io : std::uint8_t { Inherit, Pipe, Null };

struct SpawnOptions {
    fs::path cwd;                 // empty: current directory
    std::vector<std::string> env; // "KEY=VALUE" entries added to / replacing the environment
    Io in = Io::Inherit, out = Io::Inherit, err = Io::Inherit;
    bool new_session = false; // setsid: the child leads its own process group
};

struct Child {
    pid_t pid = -1;
    int out_fd = -1; // set when out == Io::Pipe
    int err_fd = -1; // set when err == Io::Pipe
};

// posix_spawnp; pid stays -1 when the program cannot be started.
auto spawn(const std::vector<std::string>& argv, const SpawnOptions& opts = {}) -> Child;
auto wait_child(pid_t pid) -> int; // exit status; 128+N for signal N

struct Result {
    int rc = 0;
    std::string out;
    std::string err;
    bool started = true;
};

// Runs to completion with stdout and stderr captured. timeout_s > 0 kills the child after that long.
auto run(const std::vector<std::string>& argv, const fs::path& cwd = {}, int timeout_s = 0) -> Result;
// Runs with the terminal attached; returns the exit status (-1 if it could not start).
auto run_inherit(const std::vector<std::string>& argv, const fs::path& cwd = {}) -> int;

auto which(std::string_view name) -> std::optional<fs::path>;
auto cpu_count() -> unsigned;

// ── Files ───────────────────────────────────────────────────────────────────
auto read_file(const fs::path& p) -> std::optional<std::string>;
void write_file(const fs::path& p, std::string_view content);
auto split_lines_keepends(std::string_view s) -> std::vector<std::string>;
auto strip(std::string_view s) -> std::string;
auto starts_with(std::string_view s, std::string_view prefix) -> bool;

// ── Hashes (hex) ────────────────────────────────────────────────────────────
auto md5_hex(std::string_view data) -> std::string;
auto sha1_hex(std::string_view data) -> std::string;

// ── Framework ───────────────────────────────────────────────────────────────
// Directory holding template/, cmake/, include/ (found from this executable's real path).
auto framework_dir() -> const fs::path&;
auto framework_version() -> const std::string&;

// ── revet.toml ──────────────────────────────────────────────────────────────
using Deps = std::vector<std::pair<std::string, std::string>>; // name -> Conan ref, in file order

auto toml_port(const fs::path& project) -> std::optional<int>;
auto project_binary_name(const fs::path& project) -> std::string;
auto read_toml() -> std::string;
void write_toml(std::string_view content);
auto deps_from_toml(std::string_view content) -> Deps;
auto find_dep(const Deps& deps, std::string_view name) -> const std::string*;
auto upsert_dep(std::string_view content, std::string_view name, std::string_view ref) -> std::string;
auto remove_dep(std::string_view content, std::string_view name) -> std::pair<std::string, bool>;

// ── Conan ───────────────────────────────────────────────────────────────────
auto framework_requirements() -> std::vector<std::string>;
auto project_conanfile(const Deps& deps) -> fs::path;
auto deps_hash(const Deps& deps) -> std::string;
auto conan_needs_update(const Deps& deps, const fs::path& conan_dir) -> bool;
void conan_mark_updated(const Deps& deps, const fs::path& conan_dir);
void conan_update_lockfile(const fs::path& conanfile);
// "name/version" lines in `conan list` output; only `name`'s when name is not empty.
auto conan_refs(std::string_view list_output, std::string_view name = {}) -> std::vector<std::string>;
void sort_refs_newest_first(std::vector<std::string>& refs);
auto conan_latest(std::string_view name, int timeout_s) -> std::optional<std::string>;
auto ref_version(std::string_view ref) -> std::string; // "fmt/10.1.0" -> "10.1.0"
auto install_deps_script() -> fs::path;
auto conan_profile() -> std::optional<std::string>;

// ── Build ───────────────────────────────────────────────────────────────────
inline constexpr std::string_view k_build_dir = "build";
void generate_cmakelists();
auto cmake_build(bool release, bool asan, bool debug, const std::optional<std::string>& target) -> Result;
auto binary_path() -> fs::path;

struct Route {
    std::string path;
    std::vector<std::string> methods;
};
auto parse_routes(std::string_view routes_cpp) -> std::vector<Route>;
void print_routes();

// ── Commands ────────────────────────────────────────────────────────────────
struct DevOptions {
    int port = 8080;
    bool lldb = false, profile = false, asan = false, debug = false;
    std::optional<std::string> logging;
};
auto cmd_dev(const DevOptions& opts) -> int;
auto cmd_packages() -> int;
auto cmd_add(std::string_view spec) -> int;
auto cmd_remove(std::string_view name) -> int;
auto cmd_lock() -> int;
auto cmd_new(std::string_view name, bool empty) -> int;
auto cmd_upgrade(bool force) -> int;
auto cmd_clean() -> int;
auto cmd_tidy() -> int;
auto cmd_format() -> int;
auto cmd_doctor() -> int;

// Critical tools for building (cmake, a C++ compiler, conan); names of the missing ones.
auto missing_critical_tools() -> std::vector<std::string>;

} // namespace revet::cli
