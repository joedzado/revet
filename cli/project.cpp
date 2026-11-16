// revet.toml, Conan, CMakeLists.txt generation, builds and the route table.
#include "cli.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctre.hpp>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace revet::cli {

// ── revet.toml ──────────────────────────────────────────────────────────────

namespace {

// First line of revet.toml matching `key = <value>` (anywhere in the file, as the old CLI did).
template <ctll::fixed_string Pattern>
std::optional<std::string> toml_first(const fs::path& project) {
    auto content = read_file(project / "revet.toml");
    if (!content) return std::nullopt;
    for (const auto& line : split_lines_keepends(*content)) {
        if (auto m = ctre::starts_with<Pattern>(line)) return m.template get<1>().to_string();
    }
    return std::nullopt;
}

bool is_dep_section(std::string_view line) { return bool(ctre::starts_with<R"RE(\s*\[build\.dependencies\])RE">(line)); }

// `<ws>["']?name["']?<ws>=` at the start of the line.
bool is_dep_line(std::string_view line, std::string_view name) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i < line.size() && (line[i] == '"' || line[i] == '\'')) ++i;
    if (line.substr(i, name.size()) != name) return false;
    i += name.size();
    if (i < line.size() && (line[i] == '"' || line[i] == '\'')) ++i;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return i < line.size() && line[i] == '=';
}

}  // namespace

std::optional<int> toml_port(const fs::path& project) {
    auto v = toml_first<R"RE(\s*port\s*=\s*(\d+))RE">(project);
    if (!v) return std::nullopt;
    return std::atoi(v->c_str());
}

std::string project_binary_name(const fs::path& project) {
    auto name = toml_first<R"RE(\s*binary\s*=\s*"([^"]+)")RE">(project);
    if (!name) return "revet";
    if (!ctre::match<R"RE([A-Za-z0-9][A-Za-z0-9._+\-]*)RE">(*name)) fail("invalid binary name in revet.toml: " + *name);
    return *name;
}

std::string read_toml() {
    auto content = read_file("revet.toml");
    if (!content) fail("cannot read revet.toml");
    return *content;
}

void write_toml(std::string_view content) { write_file("revet.toml", content); }

Deps deps_from_toml(std::string_view content) {
    Deps deps;
    bool in_deps = false;
    for (const auto& raw : split_lines_keepends(content)) {
        std::string line = strip(raw);
        if (starts_with(line, "[build.dependencies]")) {
            in_deps = true;
            continue;
        }
        if (!in_deps) continue;
        if (starts_with(line, "[")) break;
        std::string key, ref;
        if (auto m = ctre::starts_with<R"RE(["']?([A-Za-z0-9_\-]+)["']?\s*=\s*"([^"]+)")RE">(line)) {
            key = m.get<1>().to_string();
            ref = m.get<2>().to_string();
        } else if (auto b = ctre::starts_with<R"RE(["']?([A-Za-z0-9_\-]+)["']?\s*=)RE">(line)) {
            key = b.get<1>().to_string();
            ref = key;
        } else {
            continue;
        }
        auto it = std::find_if(deps.begin(), deps.end(), [&](const auto& d) { return d.first == key; });
        if (it != deps.end()) it->second = ref;
        else deps.emplace_back(key, ref);
    }
    return deps;
}

const std::string* find_dep(const Deps& deps, std::string_view name) {
    for (const auto& [k, v] : deps) {
        if (k == name) return &v;
    }
    return nullptr;
}

std::string upsert_dep(std::string_view content, std::string_view name, std::string_view ref) {
    auto lines = split_lines_keepends(content);
    std::optional<size_t> section_start, section_end, existing;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (is_dep_section(lines[i])) {
            section_start = i;
            continue;
        }
        if (section_start && !section_end) {
            std::string s = strip(lines[i]);
            if (starts_with(s, "[") && !starts_with(s, "[build.")) {
                section_end = i;
                break;
            }
            if (is_dep_line(lines[i], name)) existing = i;
        }
    }
    std::string entry = std::string(name) + " = \"" + std::string(ref) + "\"\n";

    std::string out;
    if (!section_start) {
        out = std::string(content);
        if (!out.empty() && out.back() != '\n') out += '\n';
        return out + "\n[build.dependencies]\n" + entry;
    }
    if (existing) {
        lines[*existing] = entry;
    } else {
        size_t at = section_end.value_or(lines.size());
        if (at > 0 && !lines[at - 1].empty() && lines[at - 1].back() != '\n') lines[at - 1] += '\n';
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), entry);
    }
    for (const auto& l : lines) out += l;
    return out;
}

std::pair<std::string, bool> remove_dep(std::string_view content, std::string_view name) {
    std::string out;
    bool in_deps = false, found = false;
    for (const auto& line : split_lines_keepends(content)) {
        if (is_dep_section(line)) {
            in_deps = true;
            out += line;
            continue;
        }
        if (in_deps && starts_with(strip(line), "[")) in_deps = false;
        if (in_deps && is_dep_line(line, name)) {
            found = true;
            continue;
        }
        out += line;
    }
    return {out, found};
}

// ── Conan ───────────────────────────────────────────────────────────────────

std::vector<std::string> framework_requirements() {
    std::vector<std::string> reqs;
    auto content = read_file(framework_dir() / "conanfile.txt");
    if (!content) return reqs;
    bool in_requires = false;
    for (const auto& raw : split_lines_keepends(*content)) {
        std::string line = strip(raw);
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            in_requires = line == "[requires]";
            continue;
        }
        if (in_requires) reqs.push_back(line);
    }
    return reqs;
}

fs::path project_conanfile(const Deps& deps) {
    if (deps.empty()) return framework_dir() / "conanfile.txt";
    std::vector<std::string> reqs = framework_requirements();
    std::vector<std::string> names;
    for (const auto& r : reqs) names.push_back(r.substr(0, r.find('/')));
    std::string content = "[requires]\n";
    for (const auto& r : reqs) content += r + "\n";
    for (const auto& [name, ref] : deps) {
        (void)name;
        if (std::find(names.begin(), names.end(), ref.substr(0, ref.find('/'))) == names.end()) content += ref + "\n";
    }
    content += "\n[generators]\nCMakeDeps\nCMakeToolchain\n";
    fs::path out = BUILD_DIR / "project_conanfile.txt";
    write_file(out, content);
    return out;
}

namespace {
// Python's repr() of a str, as the hash below was defined with it.
std::string py_repr(std::string_view s) {
    bool has_single = s.find('\'') != std::string_view::npos;
    bool has_double = s.find('"') != std::string_view::npos;
    char q = has_single && !has_double ? '"' : '\'';
    std::string out(1, q);
    for (char c : s) {
        if (c == '\\' || c == q) out += '\\';
        out += c;
    }
    return out + q;
}
}  // namespace

// First 8 hex digits of md5 over the app's deps and the framework's requirements, so a
// framework upgrade that changes conanfile.txt re-runs Conan too.
std::string deps_hash(const Deps& deps) {
    Deps sorted = deps;
    std::sort(sorted.begin(), sorted.end());
    std::string s = "[";
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i) s += ", ";
        s += "(" + py_repr(sorted[i].first) + ", " + py_repr(sorted[i].second) + ")";
    }
    s += "]";
    for (const auto& req : framework_requirements()) s += "\n" + req;
    return md5_hex(s).substr(0, 8);
}

bool conan_needs_update(const Deps& deps, const fs::path& conan_dir) {
    // Any file Conan always writes; a missing one means the install never finished.
    if (!fs::exists(conan_dir / "conan_toolchain.cmake")) return true;
    auto marker = read_file(conan_dir / ".revet_deps_hash");
    return !marker || strip(*marker) != deps_hash(deps);
}

void conan_mark_updated(const Deps& deps, const fs::path& conan_dir) {
    write_file(conan_dir / ".revet_deps_hash", deps_hash(deps));
}

void conan_update_lockfile(const fs::path& conanfile) {
    std::string tmpl = (fs::temp_directory_path() / "revet-lock-XXXXXX").string();
    int fd = mkstemp(tmpl.data());
    if (fd < 0) return;
    close(fd);
    Result r = run({"conan", "lock", "create", conanfile.string(), "--lockfile-out=" + tmpl});
    if (r.rc != 0) {
        dim("  (lockfile generation failed — continuing without it)");
    } else if (auto fresh = read_file(tmpl)) {
        auto current = read_file("revet.lock");
        if (!current || *current != *fresh) write_file("revet.lock", *fresh);
    }
    std::error_code ec;
    fs::remove(tmpl, ec);
}

std::vector<std::string> conan_refs(std::string_view list_output, std::string_view name) {
    std::vector<std::string> refs;
    for (const auto& raw : split_lines_keepends(list_output)) {
        std::string line = strip(raw);
        if (name.empty()) {
            if (ctre::starts_with<R"RE([a-zA-Z][a-zA-Z0-9_\-]*/\d)RE">(line)) refs.push_back(line);
        } else if (starts_with(line, std::string(name) + "/") && line.size() > name.size() + 1 &&
                   std::isdigit(static_cast<unsigned char>(line[name.size() + 1]))) {
            refs.push_back(line);
        }
    }
    return refs;
}

std::string ref_version(std::string_view ref) {
    size_t slash = ref.find('/');
    return std::string(slash == std::string_view::npos ? ref : ref.substr(slash + 1));
}

namespace {
// Numeric parts of the version, split on '.' and '-'; non-numeric parts are dropped.
std::vector<long long> version_key(std::string_view ref) {
    std::string v = ref.find('/') == std::string_view::npos ? "0" : ref_version(ref);
    std::vector<long long> key;
    size_t start = 0;
    while (start <= v.size()) {
        size_t end = v.find_first_of(".-", start);
        if (end == std::string::npos) end = v.size();
        std::string_view part = std::string_view(v).substr(start, end - start);
        if (!part.empty() && std::all_of(part.begin(), part.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            key.push_back(part.size() > 18 ? LLONG_MAX : std::stoll(std::string(part)));
        }
        start = end + 1;
    }
    return key;
}
}  // namespace

void sort_refs_newest_first(std::vector<std::string>& refs) {
    std::stable_sort(refs.begin(), refs.end(),
                     [](const std::string& a, const std::string& b) { return version_key(a) > version_key(b); });
}

std::optional<std::string> conan_latest(std::string_view name, int timeout_s) {
    Result r = run({"conan", "list", std::string(name) + "/*", "-r", "conancenter"}, {}, timeout_s);
    if (r.rc != 0) return std::nullopt;
    auto refs = conan_refs(r.out, name);
    if (refs.empty()) return std::nullopt;
    sort_refs_newest_first(refs);
    return refs.front();
}

fs::path install_deps_script() { return framework_dir() / "scripts" / "install_deps.sh"; }

// ── CMakeLists.txt ──────────────────────────────────────────────────────────

void generate_cmakelists() {
    fs::path project = fs::current_path();
    auto tmpl = read_file(framework_dir() / "template" / "CMakeLists.txt");
    if (!tmpl) fail("CMakeLists.txt template not found in framework");
    if (!fs::exists(project / "revet.toml")) fail("revet.toml not found");

    std::string name = toml_first<R"RE(\s*name\s*=\s*"([^"]+)")RE">(project).value_or(project.filename().string());
    auto replace_all = [](std::string s, std::string_view from, std::string_view to) {
        for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
        return s;
    };
    std::string content = replace_all(*tmpl, "{{name}}", name);
    content = replace_all(content, "{{framework_dir}}", framework_dir().string());
    content = replace_all(content, "{{binary}}", project_binary_name(project));
    content = "# ⚠️ GENERATED FILE - Do not edit\n"
              "# This file is generated by `revet build` from the framework template.\n"
              "# To customize the build, create a subdirectory with your own CMakeLists.txt\n\n" +
              content;
    fs::path dest = project / "CMakeLists.txt";
    if (read_file(dest) != content) write_file(dest, content);
}

fs::path binary_path() { return BUILD_DIR / project_binary_name(fs::current_path()); }

// ── Builds ──────────────────────────────────────────────────────────────────

namespace {

fs::path targets_dir() { return framework_dir() / "cmake" / "targets"; }

// A bundled target name (cmake/targets/<name>/profile) or a Conan profile path.
fs::path target_profile(const std::string& target) {
    std::error_code ec;
    if (fs::is_regular_file(target, ec)) return fs::canonical(target, ec);
    fs::path profile = targets_dir() / target / "profile";
    if (!fs::is_regular_file(profile, ec)) {
        std::vector<std::string> known;
        if (fs::is_directory(targets_dir(), ec)) {
            for (const auto& d : fs::directory_iterator(targets_dir(), ec)) {
                if (fs::is_regular_file(d.path() / "profile", ec)) known.push_back(d.path().filename().string());
            }
        }
        std::sort(known.begin(), known.end());
        std::string list;
        for (const auto& k : known) list += (list.empty() ? "" : ", ") + k;
        fail("unknown target: " + target + (list.empty() ? "" : " (available: " + list + ")"));
    }
    return profile;
}

// Profile files are keyed by resolved path: two profiles named "rpi.profile" must not share a tree.
fs::path build_dir_for(const std::string& target) {
    std::error_code ec;
    if (fs::is_regular_file(target, ec)) {
        std::string digest = sha1_hex(fs::canonical(target, ec).string()).substr(0, 8);
        return BUILD_DIR / (fs::path(target).stem().string() + "-" + digest);
    }
    return BUILD_DIR / target;
}

// Host and cross builds share build/generated (codegen output is target-independent), so builds run one at a time.
class BuildLock {
public:
    BuildLock() {
        std::error_code ec;
        fs::create_directories(BUILD_DIR, ec);
        fd_ = open((BUILD_DIR / ".build.lock").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd_ < 0) return;
        if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            std::fputs("waiting for another revet build to finish…\n", stderr);
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
    BuildLock& operator=(const BuildLock&) = delete;

private:
    int fd_ = -1;
};

Result cmake_build_locked(bool release, bool asan, bool debug, const std::optional<std::string>& target) {
    generate_cmakelists();

    // Cross builds get their own tree under build/ so they never clobber the host build.
    fs::path build_dir = target ? build_dir_for(*target) : BUILD_DIR;
    std::optional<fs::path> profile;
    if (target) profile = target_profile(*target);
    fs::path conan_dir = build_dir / "conan";
    std::error_code ec;
    fs::create_directories(conan_dir, ec);

    Deps deps = fs::exists("revet.toml") ? deps_from_toml(read_toml()) : Deps{};
    if (conan_needs_update(deps, conan_dir)) {
        if (!which("conan")) {
            err("conan not found. Run: bash " + install_deps_script().string());
            return {1, "", "conan not found", true};
        }
        info("Fetching dependencies via Conan…");
        fs::path conanfile = project_conanfile(deps);
        bool fresh_tree = !fs::exists(conan_dir / ".revet_deps_hash");
        // Release packages always: Debug builds map to the Release imports
        // (CMAKE_MAP_IMPORTED_CONFIG_DEBUG), and this keeps the user's default profile out of it.
        std::vector<std::string> cmd = {"conan", "install", conanfile.string(), "--output-folder=" + conan_dir.string(),
                                        "--build=missing", "-s", "build_type=Release"};
        if (profile) {
            cmd.push_back("-pr:h=" + profile->string());
            cmd.push_back("-pr:b=default");
        }
        // Fresh clone with a committed lockfile: use it. It describes the host graph, so cross builds skip it.
        bool with_lock = fs::exists("revet.lock") && fresh_tree && !profile;
        if (with_lock) cmd.push_back("--lockfile=revet.lock");
        Result conan = run(cmd);
        if (conan.rc != 0 && with_lock) {
            // The lockfile predates the framework's current requirements (e.g. after an upgrade).
            dim("  revet.lock is out of date with the framework; resolving without it and rewriting it");
            cmd.pop_back();
            conan = run(cmd);
        }
        if (conan.rc != 0) {
            err("conan install failed. Run: bash " + install_deps_script().string());
            return conan;
        }
        conan_mark_updated(deps, conan_dir);
        if (!profile) conan_update_lockfile(conanfile);
    }

    std::string build_type = (debug || !release) ? "Debug" : "Release";
    bool ninja = which("ninja").has_value();

    // A build tree configured for another generator has to be recreated.
    if (auto cache = read_file(build_dir / "CMakeCache.txt")) {
        bool using_ninja = cache->find("CMAKE_GENERATOR:INTERNAL=Ninja") != std::string::npos;
        if (using_ninja != ninja) {
            info("Generator changed, cleaning build directory...");
            fs::remove_all(build_dir, ec);
        }
    }

    std::vector<std::string> configure = {"cmake"};
    if (ninja) configure.insert(configure.end(), {"-G", "Ninja"});
    configure.insert(configure.end(), {"-B", build_dir.string(), "-S", ".", "-DCMAKE_BUILD_TYPE=" + build_type});
    if (profile) configure.push_back("-DCMAKE_TOOLCHAIN_FILE=" + fs::absolute(conan_dir / "conan_toolchain.cmake").string());
    if (asan) configure.push_back("-DREVET_ASAN=ON");
    Result cfg = run(configure);
    if (cfg.rc != 0) return cfg;

    Result result = run({"cmake", "--build", build_dir.string(), "--", "-j" + std::to_string(cpu_count())});
    if (result.rc != 0) {
        std::string lower = result.err;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower.find("not found") != std::string::npos || lower.find("could not find") != std::string::npos) {
            result.err += "\n\n✗ Build failed — missing dependency?\n";
            result.err += "Run: bash " + install_deps_script().string() + "\n";
            result.err += "Then: revet clean && revet dev\n";
        }
    }

    // Debug symbols for lldb on macOS (~0.2 s): only when the binary changed.
    if (!release && !profile && result.rc == 0 && which("dsymutil")) {
        fs::path binary = binary_path();
        fs::path dwarf = BUILD_DIR / (binary.filename().string() + ".dSYM") / "Contents" / "Resources" / "DWARF" /
                         binary.filename();
        if (fs::exists(binary, ec) &&
            !(fs::exists(dwarf, ec) && fs::last_write_time(dwarf, ec) >= fs::last_write_time(binary, ec))) {
            SpawnOptions quiet;
            quiet.in = quiet.out = quiet.err = Io::Null;
            Child c = spawn({"dsymutil", binary.string()}, quiet);
            if (c.pid > 0) wait_child(c.pid);
        }
    }
    return result;
}

}  // namespace

Result cmake_build(bool release, bool asan, bool debug, const std::optional<std::string>& target) {
    BuildLock lock;
    return cmake_build_locked(release, asan, debug, target);
}

// ── Routes ──────────────────────────────────────────────────────────────────

std::vector<Route> parse_routes(std::string_view src) {
    std::vector<Route> routes;
    for (auto block : ctre::search_all<R"RE(\{\s*web::Route r;[\s\S]*?out\.push_back\(r\);[\s\S]*?\})RE">(src)) {
        std::string_view b = block.to_view();
        auto path = ctre::search<R"RE(r\.path\s*=\s*"([^"]+)")RE">(b);
        if (!path) continue;
        Route r{path.get<1>().to_string(), {}};
        static const std::pair<const char*, std::array<const char*, 2>> fields[] = {
            {"GET", {"r.get", "r.async_get"}},       {"POST", {"r.post", "r.async_post"}},
            {"PUT", {"r.put", "r.async_put"}},       {"DEL", {"r.del", "r.async_del"}},
            {"PATCH", {"r.patch", "r.async_patch"}}, {"SSE", {"r.sse", "r.sse"}},
            {"WS", {"r.ws", "r.ws"}}};
        for (const auto& [method, names] : fields) {
            if (b.find(names[0]) != std::string_view::npos || b.find(names[1]) != std::string_view::npos) {
                r.methods.emplace_back(method);
            }
        }
        routes.push_back(std::move(r));
    }
    return routes;
}

void print_routes() {
    auto src = read_file(BUILD_DIR / "generated" / "routes.cpp");
    auto routes = src ? parse_routes(*src) : std::vector<Route>{};
    if (routes.empty()) {
        dim("no routes found");
        return;
    }
    for (const auto& r : routes) {
        std::string line = "  " + std::string(CYAN) + r.path + std::string(RESET) + " ";
        for (const auto& m : r.methods) {
            std::string_view color = m == "GET" ? GREEN : (m == "POST" || m == "PUT") ? YELLOW : m == "DEL" ? RED : CYAN;
            line += " " + std::string(color) + m + std::string(RESET);
        }
        info(line);
    }
}

}  // namespace revet::cli
