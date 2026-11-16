// add, remove, lock, new, upgrade, clean, tidy, format, doctor.
#include "cli.hpp"

#include <algorithm>
#include <ctre.hpp>

#include <sys/utsname.h>

namespace revet::cli {

namespace {

std::string os_name() {
    utsname u{};
    return uname(&u) == 0 ? std::string(u.sysname) : std::string();
}

void remove_quietly(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

// clang-tidy / clang-format from PATH, else Homebrew's LLVM; exits with install hints if neither.
fs::path llvm_tool(const std::string& tool, const std::string& linux_pkg) {
    if (auto p = which(tool)) return *p;
    fs::path brew = "/opt/homebrew/opt/llvm/bin/" + tool;
    if (fs::exists(brew)) return brew;
    std::string os = os_name();
    if (os == "Darwin") fail(tool + " not found. Install with: brew install llvm");
    if (os == "Linux") fail(tool + " not found. Install with: apt-get install " + linux_pkg + " (Debian/Ubuntu) or dnf install clang-tools-extra (Fedora)");
    fail(tool + " not found. Install LLVM/Clang for your platform");
}

// Sorted project sources under src/ and app/ with the given extensions.
std::vector<std::string> project_sources(std::initializer_list<std::string_view> exts) {
    std::vector<std::string> files;
    for (const char* root : {"src", "app"}) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            std::string name = it->path().filename().string();
            if (!name.empty() && name[0] == '.') {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            if (std::find(exts.begin(), exts.end(), ext) != exts.end()) files.push_back(it->path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

void require_conan() {
    if (!which("conan")) fail("conan not found — run: bash " + install_deps_script().string());
}

// "3.31.5" from `cmake version 3.31.5`; nullopt when cmake is missing or older than 3.20.
std::optional<std::string> cmake_version() {
    Result r = run({"cmake", "--version"}, {}, 5);
    if (!r.started || r.rc != 0) return std::nullopt;
    auto m = ctre::search<R"RE(cmake version (\d+)\.(\d+)[\d.]*)RE">(r.out);
    if (!m) return "found";
    int major = std::stoi(m.get<1>().to_string()), minor = std::stoi(m.get<2>().to_string());
    if (major < 3 || (major == 3 && minor < 20)) return std::nullopt;
    return m.get<0>().to_string().substr(14);
}

std::string cxx_compiler() {
    if (const char* cxx = std::getenv("CXX"); cxx && *cxx) return cxx;
    for (const char* c : {"clang++", "g++", "c++"}) {
        if (which(c)) return c;
    }
    return "";
}

}  // namespace

std::vector<std::string> missing_critical_tools() {
    std::vector<std::string> missing;
    if (!which("cmake")) missing.emplace_back("cmake");
    std::string cxx = cxx_compiler();
    if (cxx.empty() || !which(cxx)) missing.emplace_back("c++");
    if (!which("conan")) missing.emplace_back("conan");
    return missing;
}

// ── Packages ────────────────────────────────────────────────────────────────

int cmd_add(std::string_view raw_spec) {
    std::string spec = strip(raw_spec);
    std::string name;
    std::optional<std::string> ref;
    if (size_t at = spec.find('@'); at != std::string::npos) {
        name = strip(spec.substr(0, at));
        ref = name + "/" + strip(spec.substr(at + 1));
    } else if (spec.find('/') != std::string::npos && spec[0] != '/') {
        name = strip(spec.substr(0, spec.find('/')));
        ref = spec;
    } else {
        name = spec;
    }

    if (!ref) {
        require_conan();
        info("Searching Conan Center for " + name + "…");
        ref = conan_latest(name, 30);
        if (!ref) {
            err("Package '" + name + "' not found on Conan Center");
            info("  Specify version directly: revet add " + name + "@<version>");
            info("  Browse packages: https://conan.io/center/recipes/" + name);
            return 1;
        }
    }

    std::string content = read_toml();
    Deps existing = deps_from_toml(content);
    if (const std::string* cur = find_dep(existing, name); cur && *cur == *ref) {
        ok(*ref + " already in dependencies");
        return 0;
    }
    content = upsert_dep(content, name, *ref);
    write_toml(content);

    // The next build re-runs Conan; fetch now when there is already a build tree.
    fs::path conan_dir = BUILD_DIR / "conan";
    remove_quietly(conan_dir / ".revet_deps_hash");
    if (fs::exists(conan_dir) && which("conan")) {
        info("Fetching " + *ref + "…");
        Deps deps = deps_from_toml(content);
        fs::path conanfile = project_conanfile(deps);
        auto prof = conan_profile().value_or("default");
        Result r = run({"conan", "install", conanfile.string(), "--output-folder=" + conan_dir.string(), "--build=missing",
                        "-s", "build_type=Release", "-pr:h=" + prof, "-pr:b=" + prof});
        if (r.rc != 0) {
            dim("  (conan fetch failed — will retry on next build)");
        } else {
            conan_mark_updated(deps, conan_dir);
            conan_update_lockfile(conanfile);
        }
    }
    ok("added " + *ref);
    dim("  run `revet build` to rebuild with " + name);
    return 0;
}

int cmd_remove(std::string_view name) {
    auto [content, found] = remove_dep(read_toml(), name);
    if (!found) fail("'" + std::string(name) + "' not found in [build.dependencies]");
    write_toml(content);
    remove_quietly(BUILD_DIR / "conan" / ".revet_deps_hash");
    remove_quietly("revet.lock");
    ok("removed " + std::string(name));
    dim("  run `revet build` to rebuild without it");
    return 0;
}

int cmd_lock() {
    require_conan();
    fs::path conanfile = project_conanfile(deps_from_toml(read_toml()));
    info("Regenerating revet.lock…");
    auto prof = conan_profile().value_or("default");
    Result r = run({"conan", "lock", "create", conanfile.string(), "--lockfile-out=revet.lock", "-pr:h=" + prof, "-pr:b=" + prof});
    if (r.rc != 0) {
        err("Failed to generate lockfile:");
        err(r.err.empty() ? "unknown error" : r.err.substr(r.err.size() > 500 ? r.err.size() - 500 : 0));
        return 1;
    }
    ok("revet.lock updated");
    dim("  commit revet.lock to pin dependency versions for your team");
    return 0;
}

// ── Projects ────────────────────────────────────────────────────────────────

int cmd_new(std::string_view name_view, bool empty) {
    std::string name(name_view);
    const fs::path& fw = framework_dir();
    if (!fs::exists(fw / "template")) fail("framework not installed — run `./install.sh` from the revet repo first");
    fs::path dest = fs::current_path() / name;
    if (fs::exists(dest)) fail("'" + name + "' already exists");
    fs::create_directories(dest / "src");

    std::error_code ec;
    for (const char* item : {".clang-format", ".clang-tidy"}) {
        if (fs::exists(fw / item)) fs::copy_file(fw / item, dest / item, fs::copy_options::overwrite_existing, ec);
    }

    // Template files, {{name}} filled in. CMakeLists.txt is generated at build time.
    fs::path tmpl = fw / "template";
    for (auto it = fs::recursive_directory_iterator(tmpl, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        fs::path rel = it->path().lexically_relative(tmpl);
        if (rel.filename() == "CMakeLists.txt") continue;
        if (empty && (*rel.begin() == "app" || rel.filename() == "models.hpp")) continue;
        std::string content = read_file(it->path()).value_or("");
        for (size_t at = content.find("{{name}}"); at != std::string::npos; at = content.find("{{name}}", at + name.size())) {
            content.replace(at, 8, name);
        }
        write_file(dest / rel, content);
    }

    if (empty) {
        // Minimal index route so the project compiles.
        write_file(dest / "app" / "routes" / "index.cpp",
                   "#include <revet/web.hpp>\n\nstatic auto render_get(const auto&) {\n    return \"<h1>Project: " + name +
                       "</h1>\";\n}\n");
        write_file(dest / "app" / "routes" / "site_init.cpp",
                   "#include <revet/web.hpp>\n\n// Site-specific initialization\n// Called before the server starts listening\n"
                   "// Use this for database schema setup, resource initialization, etc.\nvoid site_init() {\n"
                   "    // Add your initialization code here\n}\n");
        for (const char* d : {"styles", "components", "layouts", "queries", "states"}) fs::create_directories(dest / "app" / d, ec);
        write_file(dest / "src" / "models.hpp", "#pragma once\n");
    }

    write_file(dest / ".revet_version", framework_version());
    write_file(dest / ".gitignore", "# Generated files\nCMakeLists.txt\nbuild/\n.DS_Store\n");

    for (const auto& cmd : std::vector<std::vector<std::string>>{
             {"git", "init"}, {"git", "add", "."}, {"git", "commit", "-m", "Initial commit from revet template"}}) {
        Result r = run(cmd, dest);
        if (r.rc != 0) fail(cmd[0] + " " + cmd[1] + " failed in " + dest.string() + ": " + strip(r.err));
    }

    ok("created " + name + "/");
    info("  cd " + name + " && revet dev");
    if (!which("samply")) {
        info("");
        info("  tip: install samply for flame graph profiling (revet dev --profile)");
        info("       cargo install samply && samply setup");
    }
    return 0;
}

int cmd_upgrade(bool force) {
    if (!fs::exists(framework_dir() / "template")) fail("framework not installed — run `./install.sh` from the revet repo first");
    if (!fs::exists("revet.toml")) fail("no revet.toml found — are you in a revet project?");
    const std::string& installed = framework_version();
    auto project_raw = read_file(".revet_version");
    std::optional<std::string> project = project_raw ? std::optional(strip(*project_raw)) : std::nullopt;
    if (project == installed && !force) {
        ok("using framework " + installed);
        return 0;
    }
    write_file(".revet_version", installed);
    if (project && *project != installed) {
        ok("framework updated: " + *project + " → " + installed);
        dim("  CMakeLists.txt will be regenerated at next build");
    } else {
        ok("using framework " + installed);
    }
    return 0;
}

int cmd_clean() {
    std::error_code ec;
    for (const char* p : {"build", "src/wrapped", "src/routes.cpp", "CMakeLists.txt"}) fs::remove_all(p, ec);
    return 0;
}

int cmd_tidy() {
    fs::path tidy = llvm_tool("clang-tidy", "clang-tools");
    if (!fs::exists("build/compile_commands.json")) fail("build/compile_commands.json not found — run `revet build` first");
    auto files = project_sources({".cpp"});
    if (files.empty()) fail("No source files found to tidy");
    info("Running clang-tidy on " + std::to_string(files.size()) + " files...");
    std::vector<std::string> cmd = {tidy.string(), "-p", "build", "--fix", "--fix-errors"};
    cmd.insert(cmd.end(), files.begin(), files.end());
    run_inherit(cmd);
    ok("Tidy complete");
    return 0;
}

int cmd_format() {
    fs::path format = llvm_tool("clang-format", "clang-format");
    auto files = project_sources({".cpp", ".hpp"});
    if (files.empty()) fail("No source files found to format");
    info("Formatting " + std::to_string(files.size()) + " files...");
    std::vector<std::string> cmd = {format.string(), "-i", "-j", std::to_string(cpu_count()), "--style=file"};
    cmd.insert(cmd.end(), files.begin(), files.end());
    run_inherit(cmd);
    ok("Format complete");
    return 0;
}

// ── doctor ──────────────────────────────────────────────────────────────────

int cmd_doctor() {
    struct Check {
        std::string name, why;
        bool critical;
        std::optional<std::string> version;  // nullopt: missing
    };
    auto version_of = [](std::vector<std::string> cmd, auto pattern_fn) -> std::optional<std::string> {
        Result r = run(cmd, {}, 5);
        if (!r.started) return std::nullopt;
        return pattern_fn(r.out + r.err);
    };
    auto first_number = [](const std::string& text) -> std::optional<std::string> {
        if (auto m = ctre::search<R"RE((\d+\.\d+(?:\.\d+)*))RE">(text)) return m.get<1>().to_string();
        return "found";
    };

    std::vector<Check> checks;
    checks.push_back({"cmake", "Build system (3.20+)", true, cmake_version()});

    std::string cxx = cxx_compiler();
    std::optional<std::string> cxx_ok;
    if (!cxx.empty() && which(cxx)) {
        fs::path src = fs::temp_directory_path() / "revet-doctor-cxx23.cpp";
        write_file(src, "#include <utility>\nauto f(auto x) { return x; }\nint main() { return f(0); }\n");
        Result r = run({cxx, "-std=c++23", "-c", src.string(), "-o", "/dev/null"}, {}, 20);
        remove_quietly(src);
        cxx_ok = r.rc == 0 ? std::optional<std::string>(cxx) : std::nullopt;
    }
    checks.push_back({"c++", "C++23 compiler", true, cxx_ok});
    checks.push_back({"conan", "Package manager (curl, catch2, app deps)", true,
                      version_of({"conan", "--version"}, first_number)});
    const std::pair<const char*, const char*> optional[] = {
        {"ninja", "Fast builds (fallback: make)"},       {"sqlite3", "Database (optional feature)"},
        {"clang-tidy", "Linting (revet tidy)"},          {"clang-format", "Formatting (revet format)"},
        {"ccache", "Build cache (-DREVET_CCACHE=ON)"},   {"samply", "Profiling (revet dev --profile)"},
        {"lldb", "Debugger (revet dev --lldb, d key)"},  {"bun", "Builds static/optimistic.ts"},
    };
    for (const auto& [name, why] : optional) {
        checks.push_back({name, why, false, which(name) ? version_of({name, "--version"}, first_number) : std::nullopt});
    }

    info("");
    rule("Dependency Check");
    int critical_missing = 0, optional_missing = 0;
    for (const auto& c : checks) {
        bool found = c.version.has_value();
        std::string line = std::string(found ? "✓ " : "✗ ") + c.name;
        line.resize(std::max<size_t>(line.size(), 17), ' ');
        line += c.why;
        line.resize(std::max<size_t>(line.size(), 60), ' ');
        if (found) line += " " + *c.version;
        if (!found && c.critical) line += " [CRITICAL]", ++critical_missing;
        if (!found && !c.critical) ++optional_missing;
        info(line);
    }
    if (critical_missing) {
        err(std::to_string(critical_missing) + " critical missing. Run: bash " + install_deps_script().string());
        return 1;
    }
    if (optional_missing) dim(std::to_string(optional_missing) + " optional missing (dev/profiling only)");
    return 0;
}

}  // namespace revet::cli
