#pragma once
// revet CLI: shared declarations. Each area lives in its own .cpp:
//   util.cpp      output, processes, files, hashes, framework location
//   project.cpp   revet.toml, Conan, CMakeLists.txt generation, builds, routes
//   dev.cpp       `revet dev` (file watcher, server process, keys)
//   packages.cpp  `revet packages` (FTXUI package manager)
//   commands.cpp  add, remove, lock, new, upgrade, clean, tidy, format, doctor
//   main.cpp      argument parsing and dispatch

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/types.h>

namespace revet::cli {

namespace fs = std::filesystem;

// ── Output ──────────────────────────────────────────────────────────────────
inline constexpr std::string_view RESET = "\033[0m";
inline constexpr std::string_view BOLD_CYAN = "\033[1;36m";
inline constexpr std::string_view DIM = "\033[2m";
inline constexpr std::string_view RED = "\033[91m";
inline constexpr std::string_view GREEN = "\033[92m";
inline constexpr std::string_view YELLOW = "\033[93m";
inline constexpr std::string_view CYAN = "\033[36m";

void rule(std::string_view s);  // ── s ──
void info(std::string_view s);
void err(std::string_view s);   // ERROR: s (stderr)
void dim(std::string_view s);
void ok(std::string_view s);    // ✔ s
[[noreturn]] void fail(std::string_view s);  // err + exit(1)
std::string expand_tabs(std::string_view s, int width = 4);
void clear_screen();

// ── Processes ───────────────────────────────────────────────────────────────
enum class Io { Inherit, Pipe, Null };

struct SpawnOptions {
    fs::path cwd;                     // empty: current directory
    std::vector<std::string> env;     // "KEY=VALUE" entries added to / replacing the environment
    Io in = Io::Inherit, out = Io::Inherit, err = Io::Inherit;
    bool new_session = false;         // setsid: the child leads its own process group
};

struct Child {
    pid_t pid = -1;
    int out_fd = -1;  // set when out == Io::Pipe
    int err_fd = -1;  // set when err == Io::Pipe
};

// posix_spawnp; pid stays -1 when the program cannot be started.
Child spawn(const std::vector<std::string>& argv, const SpawnOptions& opts = {});
int wait_child(pid_t pid);  // exit status; 128+N for signal N

struct Result {
    int rc = 0;
    std::string out;
    std::string err;
    bool started = true;
};

// Runs to completion with stdout and stderr captured. timeout_s > 0 kills the child after that long.
Result run(const std::vector<std::string>& argv, const fs::path& cwd = {}, int timeout_s = 0);
// Runs with the terminal attached; returns the exit status (-1 if it could not start).
int run_inherit(const std::vector<std::string>& argv, const fs::path& cwd = {});

std::optional<fs::path> which(std::string_view name);
unsigned cpu_count();

// ── Files ───────────────────────────────────────────────────────────────────
std::optional<std::string> read_file(const fs::path& p);
void write_file(const fs::path& p, std::string_view content);
std::vector<std::string> split_lines_keepends(std::string_view s);
std::string strip(std::string_view s);
bool starts_with(std::string_view s, std::string_view prefix);

// ── Hashes (hex) ────────────────────────────────────────────────────────────
std::string md5_hex(std::string_view data);
std::string sha1_hex(std::string_view data);

// ── Framework ───────────────────────────────────────────────────────────────
// Directory holding template/, cmake/, include/ (found from this executable's real path).
const fs::path& framework_dir();
const std::string& framework_version();

// ── revet.toml ──────────────────────────────────────────────────────────────
using Deps = std::vector<std::pair<std::string, std::string>>;  // name -> Conan ref, in file order

std::optional<int> toml_port(const fs::path& project);
std::string project_binary_name(const fs::path& project);
std::string read_toml();
void write_toml(std::string_view content);
Deps deps_from_toml(std::string_view content);
const std::string* find_dep(const Deps& deps, std::string_view name);
std::string upsert_dep(std::string_view content, std::string_view name, std::string_view ref);
std::pair<std::string, bool> remove_dep(std::string_view content, std::string_view name);

// ── Conan ───────────────────────────────────────────────────────────────────
std::vector<std::string> framework_requirements();
fs::path project_conanfile(const Deps& deps);
std::string deps_hash(const Deps& deps);
bool conan_needs_update(const Deps& deps, const fs::path& conan_dir);
void conan_mark_updated(const Deps& deps, const fs::path& conan_dir);
void conan_update_lockfile(const fs::path& conanfile);
// "name/version" lines in `conan list` output; only `name`'s when name is not empty.
std::vector<std::string> conan_refs(std::string_view list_output, std::string_view name = {});
void sort_refs_newest_first(std::vector<std::string>& refs);
std::optional<std::string> conan_latest(std::string_view name, int timeout_s);
std::string ref_version(std::string_view ref);  // "fmt/10.1.0" -> "10.1.0"
fs::path install_deps_script();
std::optional<std::string> conan_profile();

// ── Build ───────────────────────────────────────────────────────────────────
inline const fs::path BUILD_DIR = "build";
void generate_cmakelists();
Result cmake_build(bool release, bool asan, bool debug, const std::optional<std::string>& target);
fs::path binary_path();

struct Route {
    std::string path;
    std::vector<std::string> methods;
};
std::vector<Route> parse_routes(std::string_view routes_cpp);
void print_routes();

// ── Commands ────────────────────────────────────────────────────────────────
struct DevOptions {
    int port = 8080;
    bool lldb = false, profile = false, asan = false, debug = false;
    std::optional<std::string> logging;
};
int cmd_dev(const DevOptions& opts);
int cmd_packages();
int cmd_add(std::string_view spec);
int cmd_remove(std::string_view name);
int cmd_lock();
int cmd_new(std::string_view name, bool empty);
int cmd_upgrade(bool force);
int cmd_clean();
int cmd_tidy();
int cmd_format();
int cmd_doctor();

// Critical tools for building (cmake, a C++ compiler, conan); names of the missing ones.
std::vector<std::string> missing_critical_tools();

}  // namespace revet::cli
