// revet.toml, Conan, CMakeLists.txt generation, builds and the route table.
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctre.hpp>
#include <fcntl.h>
#include <iterator>
#include <ranges>
#include <sys/file.h>
#include <system_error>
#include <unistd.h>

#include "cli.hpp"

namespace revet::cli {

// ── revet.toml ──────────────────────────────────────────────────────────────

namespace {

// First line of revet.toml matching `key = <value>` (anywhere in the file, as the old CLI did).
template<ctll::fixed_string Pattern>
auto toml_first(const fs::path& project) -> std::optional<std::string> {
    auto content = read_file(project / "revet.toml");
    if (!content) {
        return std::nullopt;
    }
    auto lines = split_lines_keepends(*content);
    const auto line = std::ranges::find_if(
        lines, [](const std::string& l) -> bool { return static_cast<bool>(ctre::starts_with<Pattern>(l)); });
    if (line == lines.end()) {
        return std::nullopt;
    }
    return ctre::starts_with<Pattern>(*line).template get<1>().to_string();
}

auto is_dep_section(std::string_view line) -> bool {
    return static_cast<bool>(ctre::starts_with<R"RE(\s*\[build\.dependencies\])RE">(line));
}

// `<ws>["']?name["']?<ws>=` at the start of the line.
auto is_dep_line(std::string_view line, std::string_view name) -> bool {
    const auto skip_blanks = [](std::string_view s) -> std::string_view {
        return s.substr(std::min(s.find_first_not_of(" \t"), s.size()));
    };
    const auto skip_quote = [](std::string_view s) -> std::string_view {
        return s.starts_with('"') || s.starts_with('\'') ? s.substr(1) : s;
    };
    const std::string_view rest = skip_quote(skip_blanks(line));
    if (!rest.starts_with(name)) {
        return false;
    }
    return skip_blanks(skip_quote(rest.substr(name.size()))).starts_with('=');
}

auto is_section_header(std::string_view line) -> bool {
    return strip(line).starts_with('[');
}

} // namespace

auto toml_port(const fs::path& project) -> std::optional<int> {
    auto v = toml_first<R"RE(\s*port\s*=\s*(\d+))RE">(project);
    if (!v) {
        return std::nullopt;
    }
    int port = 0;
    auto [end, ec] = std::from_chars(v->data(), std::to_address(v->end()), port); // the pattern guarantees digits
    if (ec != std::errc{} || port <= 0 || port > 65535) {
        fail("invalid port in revet.toml: " + *v + " (expected 1-65535)");
    }
    return port;
}

auto project_binary_name(const fs::path& project) -> std::string {
    auto name = toml_first<R"RE(\s*binary\s*=\s*"([^"]+)")RE">(project);
    if (!name) {
        return "revet";
    }
    if (!ctre::match<R"RE([A-Za-z0-9][A-Za-z0-9._+\-]*)RE">(*name)) {
        fail("invalid binary name in revet.toml: " + *name);
    }
    return *name;
}

auto read_toml() -> std::string {
    auto content = read_file("revet.toml");
    if (!content) {
        fail("cannot read revet.toml");
    }
    return *content;
}

void write_toml(std::string_view content) {
    write_file("revet.toml", content);
}

auto deps_from_toml(std::string_view content) -> Deps {
    Deps deps;
    auto lines =
        split_lines_keepends(content) | std::views::transform(strip) | std::ranges::to<std::vector<std::string>>();
    const auto start =
        std::ranges::find_if(lines, [](const std::string& l) -> bool { return l.starts_with("[build.dependencies]"); });
    if (start == lines.end()) {
        return deps;
    }
    const auto section = std::ranges::subrange(std::next(start), lines.end()) |
                         std::views::take_while([](const std::string& l) -> bool { return !l.starts_with('['); });
    for (const auto& line : section) {
        std::string key;
        std::string ref;
        if (const auto m = ctre::starts_with<R"RE(["']?([A-Za-z0-9_\-]+)["']?\s*=\s*"([^"]+)")RE">(line)) {
            key = m.get<1>().to_string();
            ref = m.get<2>().to_string();
        } else if (const auto b = ctre::starts_with<R"RE(["']?([A-Za-z0-9_\-]+)["']?\s*=)RE">(line)) {
            key = b.get<1>().to_string();
            ref = key;
        } else {
            continue;
        }
        const auto it = std::ranges::find_if(deps, [&](const auto& d) -> auto { return d.first == key; });
        if (it != deps.end()) {
            it->second = ref;
        } else {
            deps.emplace_back(key, ref);
        }
    }
    return deps;
}

auto find_dep(const Deps& deps, std::string_view name) -> const std::string* {
    const auto it = std::ranges::find(deps, name, &Deps::value_type::first);
    return it == deps.end() ? nullptr : &it->second;
}

auto upsert_dep(std::string_view content, std::string_view name, std::string_view ref) -> std::string {
    auto lines = split_lines_keepends(content);
    std::string entry = std::string(name) + " = \"" + std::string(ref) + "\"\n";
    const auto start = std::ranges::find_if(lines, is_dep_section);
    if (start == lines.end()) {
        std::string out(content);
        if (!out.empty() && out.back() != '\n') {
            out += '\n';
        }
        return out + "\n[build.dependencies]\n" + entry;
    }
    const auto end = std::find_if(std::next(start), lines.end(), is_section_header);
    // The last matching line wins, as in deps_from_toml.
    auto body = std::ranges::subrange(std::next(start), end) | std::views::reverse;
    if (const auto existing =
            std::ranges::find_if(body, [&](const std::string& l) -> bool { return is_dep_line(l, name); });
        existing != body.end()) {
        *existing = std::move(entry);
    } else {
        if (auto& last = *std::prev(end); !last.empty() && last.back() != '\n') {
            last += '\n';
        }
        lines.insert(end, std::move(entry));
    }
    return join(lines, "");
}

auto remove_dep(std::string_view content, std::string_view name) -> std::pair<std::string, bool> {
    auto lines = split_lines_keepends(content);
    const auto start = std::ranges::find_if(lines, is_dep_section);
    if (start == lines.end()) {
        return {std::string(content), false};
    }
    const auto end = std::find_if(std::next(start), lines.end(), is_section_header);
    const auto removed = std::ranges::remove_if(std::next(start), end,
                                                [&](const std::string& l) -> bool { return is_dep_line(l, name); });
    const bool found = !removed.empty();
    lines.erase(removed.begin(), removed.end());
    return {join(lines, ""), found};
}

// ── Conan ───────────────────────────────────────────────────────────────────

auto framework_requirements() -> std::vector<std::string> {
    auto content = read_file(framework_dir() / "conanfile.txt");
    if (!content) {
        return {};
    }
    auto lines = split_lines_keepends(*content) | std::views::transform(strip) |
                 std::views::filter([](const std::string& l) -> bool { return !l.empty() && !l.starts_with('#'); }) |
                 std::ranges::to<std::vector<std::string>>();
    const auto start = std::ranges::find(lines, "[requires]");
    if (start == lines.end()) {
        return {};
    }
    return std::ranges::subrange(std::next(start), lines.end()) |
           std::views::take_while([](const std::string& l) -> bool { return !l.starts_with('['); }) |
           std::ranges::to<std::vector<std::string>>();
}

auto project_conanfile(const Deps& deps) -> fs::path {
    if (deps.empty()) {
        return framework_dir() / "conanfile.txt";
    }
    std::vector<std::string> reqs = framework_requirements();
    auto name_of = [](const std::string& ref) -> std::string { return ref.substr(0, ref.find('/')); };
    auto names = reqs | std::views::transform(name_of) | std::ranges::to<std::vector<std::string>>();
    std::ranges::copy(deps | std::views::values | std::views::filter([&](const std::string& ref) -> bool {
                          return !std::ranges::contains(names, name_of(ref));
                      }),
                      std::back_inserter(reqs));
    const std::string content = "[requires]\n" + join(reqs, "\n") + "\n\n[generators]\nCMakeDeps\nCMakeToolchain\n";
    fs::path out = fs::path(k_build_dir) / "project_conanfile.txt";
    write_file(out, content);
    return out;
}

namespace {
// A quoted string literal ('...' unless that needs escaping), the format deps_hash has always hashed, so
// existing .revet_deps_hash markers stay valid.
auto py_repr(std::string_view s) -> std::string {
    const bool has_single = s.contains('\'');
    const bool has_double = s.contains('"');
    const char q = has_single && !has_double ? '"' : '\'';
    return std::ranges::fold_left(s, std::string(1, q),
                                  [q](std::string acc, char c) -> std::string {
                                      if (c == '\\' || c == q) {
                                          acc += '\\';
                                      }
                                      acc += c;
                                      return acc;
                                  }) +
           q;
}
} // namespace

// First 8 hex digits of md5 over the app's deps and the framework's requirements, so a
// framework upgrade that changes conanfile.txt re-runs Conan too.
auto deps_hash(const Deps& deps) -> std::string {
    Deps sorted = deps;
    std::ranges::sort(sorted);
    const auto pairs = sorted | std::views::transform([](const auto& d) -> std::string {
                           return "(" + py_repr(d.first) + ", " + py_repr(d.second) + ")";
                       }) |
                       std::ranges::to<std::vector<std::string>>();
    std::string s = "[" + join(pairs, ", ") + "]";
    for (const auto& req : framework_requirements()) {
        s += "\n" + req;
    }
    return md5_hex(s).substr(0, 8);
}

auto conan_needs_update(const Deps& deps, const fs::path& conan_dir) -> bool {
    // Any file Conan always writes; a missing one means the install never finished.
    if (!fs::exists(conan_dir / "conan_toolchain.cmake")) {
        return true;
    }
    auto marker = read_file(conan_dir / ".revet_deps_hash");
    return !marker || strip(*marker) != deps_hash(deps);
}

void conan_mark_updated(const Deps& deps, const fs::path& conan_dir) {
    write_file(conan_dir / ".revet_deps_hash", deps_hash(deps));
}

void conan_update_lockfile(const fs::path& conanfile) {
    std::string tmpl = (fs::temp_directory_path() / "revet-lock-XXXXXX").string();
    const int fd = mkstemp(tmpl.data());
    if (fd < 0) {
        return;
    }
    close(fd);
    const auto prof = conan_profile().value_or("default");
    const Result r = run(
        {"conan", "lock", "create", conanfile.string(), "--lockfile-out=" + tmpl, "-pr:h=" + prof, "-pr:b=" + prof});
    if (r.rc != 0) {
        dim("  (lockfile generation failed — continuing without it)");
    } else if (auto fresh = read_file(tmpl)) {
        auto current = read_file("revet.lock");
        if (!current || *current != *fresh) {
            write_file("revet.lock", *fresh);
        }
    }
    std::error_code ec;
    fs::remove(tmpl, ec);
}

auto conan_refs(std::string_view list_output, std::string_view name) -> std::vector<std::string> {
    auto is_ref = [name](const std::string& line) -> bool {
        if (name.empty()) {
            return static_cast<bool>(ctre::starts_with<R"RE([a-zA-Z][a-zA-Z0-9_\-]*/\d)RE">(line));
        }
        return starts_with(line, std::string(name) + "/") && line.size() > name.size() + 1 &&
               (std::isdigit(static_cast<unsigned char>(line.at(name.size() + 1))) != 0);
    };
    return split_lines_keepends(list_output) | std::views::transform(strip) | std::views::filter(is_ref) |
           std::ranges::to<std::vector<std::string>>();
}

auto ref_version(std::string_view ref) -> std::string {
    const size_t slash = ref.find('/');
    return std::string(slash == std::string_view::npos ? ref : ref.substr(slash + 1));
}

namespace {
// Numeric parts of the version, split on '.' and '-'; non-numeric parts are dropped.
auto version_key(std::string_view ref) -> std::vector<long long> {
    std::string v = !ref.contains('/') ? "0" : ref_version(ref);
    std::ranges::replace(v, '-', '.');
    auto numeric = [](std::string_view part) -> bool {
        return !part.empty() && std::ranges::all_of(part, [](char c) -> bool {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
    };
    return v | std::views::split('.') |
           std::views::transform([](auto part) -> std::string_view { return std::string_view(part); }) |
           std::views::filter(numeric) | std::views::transform([](std::string_view part) -> long long {
               return part.size() > 18 ? LLONG_MAX : std::stoll(std::string(part));
           }) |
           std::ranges::to<std::vector<long long>>();
}
} // namespace

void sort_refs_newest_first(std::vector<std::string>& refs) {
    std::ranges::stable_sort(
        refs, [](const std::string& a, const std::string& b) -> bool { return version_key(a) > version_key(b); });
}

auto conan_latest(std::string_view name, int timeout_s, const std::stop_token& stop) -> std::optional<std::string> {
    const Result r = run({"conan", "list", std::string(name) + "/*", "-r", "conancenter"}, {}, timeout_s, stop);
    if (r.rc != 0) {
        return std::nullopt;
    }
    auto refs = conan_refs(r.out, name);
    if (refs.empty()) {
        return std::nullopt;
    }
    sort_refs_newest_first(refs);
    return refs.front();
}

auto install_deps_script() -> fs::path {
    return framework_dir() / "scripts" / "install_deps.sh";
}

// revet's own Conan profile ("revet"), detected once with the compiler revet builds with
// (CC/CXX; Apple clang on macOS, see main). The user's default profile can name any
// compiler — `cc` on PATH may even be GCC — and must not decide how revet's packages build.
auto conan_profile() -> std::optional<std::string> {
    static std::optional<std::string> cached;
    if (cached) {
        return cached;
    }
    const Result home = run({"conan", "config", "home"}, {}, 30);
    if (home.rc != 0) {
        return std::nullopt;
    }
    const fs::path path = fs::path(strip(home.out)) / "profiles" / "revet";
    if (!fs::exists(path)) {
        const Result r = run({"conan", "profile", "detect", "--name", "revet"}, {}, 60);
        if (r.rc != 0) {
            err("conan profile detect failed: " + strip(r.err));
            return std::nullopt;
        }
    }
    cached = "revet";
    return cached;
}

namespace {
// The last `n` lines of `s`.
auto tail_lines(const std::string& s, size_t n) -> std::string {
    auto lines = split_lines_keepends(s);
    return join(lines | std::views::drop(lines.size() - std::min(n, lines.size())) |
                    std::ranges::to<std::vector<std::string>>(),
                "");
}
} // namespace

// ── CMakeLists.txt ──────────────────────────────────────────────────────────

void generate_cmakelists() {
    const fs::path project = fs::current_path();
    auto tmpl = read_file(framework_dir() / "template" / "CMakeLists.txt");
    if (!tmpl) {
        fail("CMakeLists.txt template not found in framework");
    }
    if (!fs::exists(project / "revet.toml")) {
        fail("revet.toml not found");
    }

    const std::string name =
        toml_first<R"RE(\s*name\s*=\s*"([^"]+)")RE">(project).value_or(project.filename().string());
    std::string content = replace_all(*tmpl, "{{name}}", name);
    content = replace_all(content, "{{framework_dir}}", framework_dir().string());
    content = replace_all(content, "{{binary}}", project_binary_name(project));
    content = "# ⚠️ GENERATED FILE - Do not edit\n"
              "# This file is generated by `revet build` from the framework template.\n"
              "# To customize the build, create a subdirectory with your own CMakeLists.txt\n\n" +
              content;
    const fs::path dest = project / "CMakeLists.txt";
    if (read_file(dest) != content) {
        write_file(dest, content);
    }
}

auto binary_path() -> fs::path {
    return fs::path(k_build_dir) / project_binary_name(fs::current_path());
}

// ── Builds ──────────────────────────────────────────────────────────────────

namespace {

auto targets_dir() -> fs::path {
    return framework_dir() / "cmake" / "targets";
}

// A bundled target name (cmake/targets/<name>/profile) or a Conan profile path.
auto target_profile(const std::string& target) -> fs::path {
    std::error_code ec;
    if (fs::is_regular_file(target, ec)) {
        return fs::canonical(target, ec);
    }
    fs::path profile = targets_dir() / target / "profile";
    if (!fs::is_regular_file(profile, ec)) {
        std::vector<std::string> known;
        if (fs::is_directory(targets_dir(), ec)) {
            known = fs::directory_iterator(targets_dir(), ec) |
                    std::views::filter([&](const fs::directory_entry& d) -> bool {
                        return fs::is_regular_file(d.path() / "profile", ec);
                    }) |
                    std::views::transform(
                        [](const fs::directory_entry& d) -> std::string { return d.path().filename().string(); }) |
                    std::ranges::to<std::vector<std::string>>();
        }
        std::ranges::sort(known);
        const std::string list = join(known, ", ");
        fail("unknown target: " + target + (list.empty() ? "" : " (available: " + list + ")"));
    }
    return profile;
}

// Profile files are keyed by resolved path: two profiles named "rpi.profile" must not share a tree.
auto build_dir_for(const std::string& target) -> fs::path {
    std::error_code ec;
    if (fs::is_regular_file(target, ec)) {
        const std::string digest = sha1_hex(fs::canonical(target, ec).string()).substr(0, 8);
        return fs::path(k_build_dir) / (fs::path(target).stem().string() + "-" + digest);
    }
    return fs::path(k_build_dir) / target;
}

// Host and cross builds share build/generated (codegen output is target-independent), so builds run one at a time.
class BuildLock {
public:
    BuildLock() {
        std::error_code ec;
        fs::create_directories(k_build_dir, ec);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open
        fd_ = open((fs::path(k_build_dir) / ".build.lock").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd_ < 0) {
            return;
        }
        if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            (void)std::fputs("waiting for another revet build to finish…\n", stderr);
            flock(fd_, LOCK_EX);
        }
    }
    ~BuildLock() {
        if (fd_ >= 0) {
            flock(fd_, LOCK_UN);
            close(fd_);
        }
    }
    BuildLock(const BuildLock&) = delete;
    BuildLock(BuildLock&&) = delete;
    auto operator=(BuildLock&&) -> BuildLock& = delete;
    auto operator=(const BuildLock&) -> BuildLock& = delete;

private:
    int fd_ = -1;
};

auto cmake_build_locked(bool release, bool asan, bool debug, const std::optional<std::string>& target) -> Result {
    generate_cmakelists();

    // Cross builds get their own tree under build/ so they never clobber the host build.
    const fs::path build_dir = target ? build_dir_for(*target) : k_build_dir;
    std::optional<fs::path> profile;
    if (target) {
        profile = target_profile(*target);
    }
    const fs::path conan_dir = build_dir / "conan";
    std::error_code ec;
    fs::create_directories(conan_dir, ec);

    const Deps deps = fs::exists("revet.toml") ? deps_from_toml(read_toml()) : Deps{};
    if (conan_needs_update(deps, conan_dir)) {
        if (!which("conan")) {
            err("conan not found. Run: bash " + install_deps_script().string());
            return {.rc = 1, .out = "", .err = "conan not found", .started = true};
        }
        info("Fetching dependencies via Conan…");
        const fs::path conanfile = project_conanfile(deps);
        const bool fresh_tree = !fs::exists(conan_dir / ".revet_deps_hash");
        auto revet_profile = conan_profile();
        if (!revet_profile) {
            return {.rc = 1, .out = "", .err = "no usable Conan profile", .started = true};
        }
        // Release packages always: Debug builds map to the Release imports (CMAKE_MAP_IMPORTED_CONFIG_DEBUG).
        std::vector<std::string> cmd = {
            "conan",           "install", conanfile.string(),  "--output-folder=" + conan_dir.string(),
            "--build=missing", "-s",      "build_type=Release"};
        cmd.push_back("-pr:h=" + (profile ? profile->string() : *revet_profile));
        cmd.push_back("-pr:b=" + *revet_profile);
        // Fresh clone with a committed lockfile: use it. It describes the host graph, so cross builds skip it.
        const bool with_lock = fs::exists("revet.lock") && fresh_tree && !profile;
        if (with_lock) {
            cmd.emplace_back("--lockfile=revet.lock");
        }
        Result conan = run(cmd);
        if (conan.rc != 0 && with_lock) {
            // The lockfile predates the framework's current requirements (e.g. after an upgrade).
            dim("  revet.lock is out of date with the framework; resolving without it and rewriting it");
            cmd.pop_back();
            conan = run(cmd);
        }
        if (conan.rc != 0) {
            // Conan's full log is long; the end has the error.
            conan.err = tail_lines(conan.out + conan.err, 40) + "\nconan install failed (" + conanfile.string() + ")";
            conan.out.clear();
            return conan;
        }
        conan_mark_updated(deps, conan_dir);
        if (!profile) {
            conan_update_lockfile(conanfile);
        }
    }

    const std::string build_type = (debug || !release) ? "Debug" : "Release";
    const bool ninja = which("ninja").has_value();

    // A build tree configured for another generator has to be recreated.
    if (auto cache = read_file(build_dir / "CMakeCache.txt")) {
        const bool using_ninja = cache->contains("CMAKE_GENERATOR:INTERNAL=Ninja");
        if (using_ninja != ninja) {
            info("Generator changed, cleaning build directory...");
            fs::remove_all(build_dir, ec);
        }
    }

    std::vector<std::string> configure = {"cmake"};
    if (ninja) {
        configure.insert(configure.end(), {"-G", "Ninja"});
    }
    configure.insert(configure.end(), {"-B", build_dir.string(), "-S", ".", "-DCMAKE_BUILD_TYPE=" + build_type});
    if (profile) {
        configure.push_back("-DCMAKE_TOOLCHAIN_FILE=" + fs::absolute(conan_dir / "conan_toolchain.cmake").string());
    }
    if (asan) {
        configure.emplace_back("-DREVET_ASAN=ON");
    }
    Result cfg = run(configure);
    if (cfg.rc != 0) {
        return cfg;
    }

    Result result = run({"cmake", "--build", build_dir.string(), "--", "-j" + std::to_string(cpu_count())});
    if (result.rc != 0) {
        std::string lower = result.err;
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) -> int { return std::tolower(c); });
        if (lower.contains("not found") || lower.contains("could not find")) {
            result.err += "\n\n✗ Build failed — missing dependency?\n";
            result.err += "Run: bash " + install_deps_script().string() + "\n";
            result.err += "Then: revet clean && revet dev\n";
        }
    }

    // Debug symbols for lldb on macOS (~0.2 s): only when the binary changed.
    if (!release && !profile && result.rc == 0 && which("dsymutil")) {
        const fs::path binary = binary_path();
        const fs::path dwarf = fs::path(k_build_dir) / (binary.filename().string() + ".dSYM") / "Contents" /
                               "Resources" / "DWARF" / binary.filename();
        if (fs::exists(binary, ec) &&
            !(fs::exists(dwarf, ec) && fs::last_write_time(dwarf, ec) >= fs::last_write_time(binary, ec))) {
            SpawnOptions quiet;
            quiet.in = quiet.out = quiet.err = Io::Null;
            const Child c = spawn({"dsymutil", binary.string()}, quiet);
            if (c.pid > 0) {
                wait_child(c.pid);
            }
        }
    }
    return result;
}

} // namespace

auto cmake_build(bool release, bool asan, bool debug, const std::optional<std::string>& target) -> Result {
    const BuildLock lock;
    return cmake_build_locked(release, asan, debug, target);
}

// ── Routes ──────────────────────────────────────────────────────────────────

auto parse_routes(std::string_view routes_cpp) -> std::vector<Route> {
    std::vector<Route> routes;
    for (const auto block :
         ctre::search_all<R"RE(\{\s*web::Route r;[\s\S]*?out\.push_back\(r\);[\s\S]*?\})RE">(routes_cpp)) {
        std::string_view b = block.to_view();
        const auto path = ctre::search<R"RE(r\.path\s*=\s*"([^"]+)")RE">(b);
        if (!path) {
            continue;
        }
        Route r{.path = path.get<1>().to_string(), .methods = {}};
        static constexpr auto fields = std::to_array<std::pair<const char*, std::array<const char*, 2>>>({
            {"GET", {"r.get", "r.async_get"}},
            {"POST", {"r.post", "r.async_post"}},
            {"PUT", {"r.put", "r.async_put"}},
            {"DEL", {"r.del", "r.async_del"}},
            {"PATCH", {"r.patch", "r.async_patch"}},
            {"SSE", {"r.sse", "r.sse"}},
            {"WS", {"r.ws", "r.ws"}},
        });
        r.methods = fields | std::views::filter([&](const auto& f) -> bool {
                        return b.contains(f.second.at(0)) || b.contains(f.second.at(1));
                    }) |
                    std::views::keys | std::ranges::to<std::vector<std::string>>();
        routes.push_back(std::move(r));
    }
    return routes;
}

void print_routes() {
    auto src = read_file(fs::path(k_build_dir) / "generated" / "routes.cpp");
    const auto routes = src ? parse_routes(*src) : std::vector<Route>{};
    if (routes.empty()) {
        dim("no routes found");
        return;
    }
    for (const auto& r : routes) {
        auto colored = [](const std::string& m) -> std::string {
            std::string_view color = k_cyan;
            if (m == "GET") {
                color = k_green;
            } else if (m == "POST" || m == "PUT") {
                color = k_yellow;
            } else if (m == "DEL") {
                color = k_red;
            }
            return " " + std::string(color) + m + std::string(k_reset);
        };
        info("  " + std::string(k_cyan) + r.path + std::string(k_reset) + " " +
             join(r.methods | std::views::transform(colored) | std::ranges::to<std::vector<std::string>>(), ""));
    }
}

} // namespace revet::cli
