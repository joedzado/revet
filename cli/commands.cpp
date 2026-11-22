// add, remove, lock, new, upgrade, clean, tidy, format, doctor.
#include <algorithm>
#include <array>
#include <ctre.hpp>
#include <iterator>
#include <ranges>
#include <sys/utsname.h>

#include "cli.hpp"

namespace revet::cli {

namespace {

auto os_name() -> std::string {
    utsname u{};
    return uname(&u) == 0 ? std::string(std::data(u.sysname)) : std::string();
}

void remove_quietly(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

// clang-tidy / clang-format from PATH, else Homebrew's LLVM; exits with install hints if neither.
auto llvm_tool(const std::string& tool, const std::string& linux_pkg) -> fs::path {
    if (auto p = which(tool)) {
        return *p;
    }
    fs::path brew = "/opt/homebrew/opt/llvm/bin/" + tool;
    if (fs::exists(brew)) {
        return brew;
    }
    const std::string os = os_name();
    if (os == "Darwin") {
        fail(tool + " not found. Install with: brew install llvm");
    }
    if (os == "Linux") {
        fail(tool + " not found. Install with: apt-get install " + linux_pkg +
             " (Debian/Ubuntu) or dnf install clang-tools-extra (Fedora)");
    }
    fail(tool + " not found. Install LLVM/Clang for your platform");
}

// Sorted project sources under src/ and app/ with the given extensions.
auto project_sources(std::initializer_list<std::string_view> exts) -> std::vector<std::string> {
    auto paths = files_under("src", is_hidden);
    std::ranges::copy(files_under("app", is_hidden), std::back_inserter(paths));
    auto files = paths | std::views::filter([&](const fs::path& p) -> bool {
                     return std::ranges::contains(exts, std::string_view(p.extension().string()));
                 }) |
                 std::views::transform([](const fs::path& p) -> std::string { return p.string(); }) |
                 std::ranges::to<std::vector<std::string>>();
    std::ranges::sort(files);
    return files;
}

void require_conan() {
    if (!which("conan")) {
        fail("conan not found — run: bash " + install_deps_script().string());
    }
}

// "3.31.5" from `cmake version 3.31.5`; nullopt when cmake is missing or older than 3.20.
auto cmake_version() -> std::optional<std::string> {
    Result r = run({"cmake", "--version"}, {}, 5);
    if (!r.started || r.rc != 0) {
        return std::nullopt;
    }
    const auto m = ctre::search<R"RE(cmake version (\d+)\.(\d+)[\d.]*)RE">(r.out);
    if (!m) {
        return "found";
    }
    const int major = std::stoi(m.get<1>().to_string());
    const int minor = std::stoi(m.get<2>().to_string());
    if (major < 3 || (major == 3 && minor < 20)) {
        return std::nullopt;
    }
    return m.get<0>().to_string().substr(14);
}

auto cxx_compiler() -> std::string {
    if (const char* cxx = std::getenv("CXX"); cxx != nullptr && *cxx != 0) {
        return cxx;
    }
    constexpr auto k_compilers = std::to_array<std::string_view>({"clang++", "g++", "c++"});
    const auto* found =
        std::ranges::find_if(k_compilers, [](std::string_view c) -> bool { return which(c).has_value(); });
    return found == k_compilers.end() ? "" : std::string(*found);
}

} // namespace

auto missing_critical_tools() -> std::vector<std::string> {
    std::vector<std::string> missing;
    if (!which("cmake")) {
        missing.emplace_back("cmake");
    }
    const std::string cxx = cxx_compiler();
    if (cxx.empty() || !which(cxx)) {
        missing.emplace_back("c++");
    }
    if (!which("conan")) {
        missing.emplace_back("conan");
    }
    return missing;
}

// ── Packages ────────────────────────────────────────────────────────────────

auto cmd_add(std::string_view raw_spec) -> int {
    std::string spec = strip(raw_spec);
    std::string name;
    std::optional<std::string> ref;
    if (const size_t at = spec.find('@'); at != std::string::npos) {
        name = strip(spec.substr(0, at));
        ref = name + "/" + strip(spec.substr(at + 1));
    } else if (spec.contains('/') && spec.at(0) != '/') {
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
    const Deps existing = deps_from_toml(content);
    if (const std::string* cur = find_dep(existing, name); cur != nullptr && *cur == *ref) {
        ok(*ref + " already in dependencies");
        return 0;
    }
    content = upsert_dep(content, name, *ref);
    write_toml(content);

    // The next build re-runs Conan; fetch now when there is already a build tree.
    const fs::path conan_dir = fs::path(k_build_dir) / "conan";
    remove_quietly(conan_dir / ".revet_deps_hash");
    if (fs::exists(conan_dir) && which("conan")) {
        info("Fetching " + *ref + "…");
        const Deps deps = deps_from_toml(content);
        const fs::path conanfile = project_conanfile(deps);
        const auto prof = conan_profile().value_or("default");
        const Result r = run({"conan", "install", conanfile.string(), "--output-folder=" + conan_dir.string(),
                              "--build=missing", "-s", "build_type=Release", "-pr:h=" + prof, "-pr:b=" + prof});
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

auto cmd_remove(std::string_view name) -> int {
    auto [content, found] = remove_dep(read_toml(), name);
    if (!found) {
        fail("'" + std::string(name) + "' not found in [build.dependencies]");
    }
    write_toml(content);
    remove_quietly(fs::path(k_build_dir) / "conan" / ".revet_deps_hash");
    remove_quietly("revet.lock");
    ok("removed " + std::string(name));
    dim("  run `revet build` to rebuild without it");
    return 0;
}

auto cmd_lock() -> int {
    require_conan();
    const fs::path conanfile = project_conanfile(deps_from_toml(read_toml()));
    info("Regenerating revet.lock…");
    const auto prof = conan_profile().value_or("default");
    const Result r = run(
        {"conan", "lock", "create", conanfile.string(), "--lockfile-out=revet.lock", "-pr:h=" + prof, "-pr:b=" + prof});
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

auto cmd_new(std::string_view name_view, bool empty) -> int {
    const std::string name(name_view);
    const fs::path& fw = framework_dir();
    if (!fs::exists(fw / "template")) {
        fail("framework not installed — run `./install.sh` from the revet repo first");
    }
    const fs::path dest = fs::current_path() / name;
    if (fs::exists(dest)) {
        fail("'" + name + "' already exists");
    }
    fs::create_directories(dest / "src");

    std::error_code ec;
    for (const char* item : {".clang-format", ".clang-tidy"}) {
        if (fs::exists(fw / item)) {
            fs::copy_file(fw / item, dest / item, fs::copy_options::overwrite_existing, ec);
        }
    }

    // Template files, {{name}} filled in. CMakeLists.txt is generated at build time.
    const fs::path tmpl = fw / "template";
    const auto wanted = [&](const fs::path& rel) -> bool {
        return rel.filename() != "CMakeLists.txt" &&
               !(empty && (*rel.begin() == "app" || rel.filename() == "models.hpp"));
    };
    for (const auto& file : files_under(tmpl)) {
        if (const fs::path rel = file.lexically_relative(tmpl); wanted(rel)) {
            write_file(dest / rel, replace_all(read_file(file).value_or(""), "{{name}}", name));
        }
    }

    if (empty) {
        // Minimal index route so the project compiles.
        write_file(dest / "app" / "routes" / "index.cpp",
                   "#include <revet/web.hpp>\n\nstatic auto render_get(const auto&) {\n    return \"<h1>Project: " +
                       name + "</h1>\";\n}\n");
        write_file(dest / "app" / "routes" / "site_init.cpp",
                   "#include <revet/web.hpp>\n\n// Site-specific initialization\n// Called before the server starts "
                   "listening\n"
                   "// Use this for database schema setup, resource initialization, etc.\nvoid site_init() {\n"
                   "    // Add your initialization code here\n}\n");
        for (const char* d : {"styles", "components", "layouts", "queries", "states"}) {
            fs::create_directories(dest / "app" / d, ec);
        }
        write_file(dest / "src" / "models.hpp", "#pragma once\n");
    }

    write_file(dest / ".revet_version", framework_version());
    write_file(dest / ".gitignore", "# Generated files\nCMakeLists.txt\nbuild/\n.DS_Store\n");

    for (const auto& cmd : std::vector<std::vector<std::string>>{
             {"git", "init"}, {"git", "add", "."}, {"git", "commit", "-m", "Initial commit from revet template"}}) {
        const Result r = run(cmd, dest);
        if (r.rc != 0) {
            fail(cmd.at(0) + " " + cmd.at(1) + " failed in " + dest.string() + ": " + strip(r.err));
        }
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

auto cmd_upgrade(bool force) -> int {
    if (!fs::exists(framework_dir() / "template")) {
        fail("framework not installed — run `./install.sh` from the revet repo first");
    }
    if (!fs::exists("revet.toml")) {
        fail("no revet.toml found — are you in a revet project?");
    }
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

auto cmd_clean() -> int {
    std::error_code ec;
    for (const char* p : {"build", "src/wrapped", "src/routes.cpp", "CMakeLists.txt"}) {
        fs::remove_all(p, ec);
    }
    return 0;
}

// Drops "-Xclang -include-pch -Xclang <file>.pch" from a compile database: a PCH built by
// one clang can't be read by another clang-tidy, which then fails every file.
auto strip_pch(std::string db) -> std::string {
    constexpr std::string_view k_flag = "-Xclang -include-pch -Xclang ";
    constexpr std::string_view k_ext = ".pch";
    for (std::size_t at = db.find(k_flag); at != std::string::npos; at = db.find(k_flag, at)) {
        std::size_t end = db.find(k_ext, at + k_flag.size());
        if (end == std::string::npos) {
            break;
        }
        end += k_ext.size();
        if (end < db.size() && db.at(end) == ' ') {
            ++end;
        }
        db.erase(at, end - at);
    }
    return db;
}

auto cmd_tidy() -> int {
    const fs::path tidy = llvm_tool("clang-tidy", "clang-tools");
    auto db = read_file(fs::path(k_build_dir) / "compile_commands.json");
    if (!db) {
        fail("build/compile_commands.json not found — run `revet build` first");
    }
    const fs::path tidy_db = fs::path(k_build_dir) / "tidy";
    fs::create_directories(tidy_db);
    write_file(tidy_db / "compile_commands.json", strip_pch(*db));
    // Route files include "json_models.hpp", which codegen strips when it inlines them; point it at models.hpp.
    write_file(tidy_db / "json_models.hpp", "#pragma once\n#include \"models.hpp\"\n");
    auto files = project_sources({".cpp"});
    if (files.empty()) {
        fail("No source files found to tidy");
    }
    info("Running clang-tidy on " + std::to_string(files.size()) + " files...");
    std::vector<std::string> cmd = {tidy.string(), "-p", tidy_db.string(),
                                    "--extra-arg=-I" + fs::absolute(tidy_db).string(), "--fix"};
    cmd.insert(cmd.end(), files.begin(), files.end());
    if (const int rc = run_inherit(cmd); rc != 0) {
        err("clang-tidy failed (exit " + std::to_string(rc) + ")");
        return 1;
    }
    ok("Tidy complete");
    return 0;
}

auto cmd_format() -> int {
    const fs::path format = llvm_tool("clang-format", "clang-format");
    auto files = project_sources({".cpp", ".hpp"});
    if (files.empty()) {
        fail("No source files found to format");
    }
    info("Formatting " + std::to_string(files.size()) + " files...");
    std::vector<std::string> cmd = {format.string(), "-i", "--style=file"};
    cmd.insert(cmd.end(), files.begin(), files.end());
    if (const int rc = run_inherit(cmd); rc != 0) {
        err("clang-format failed (exit " + std::to_string(rc) + ")");
        return 1;
    }
    ok("Format complete");
    return 0;
}

// ── doctor ──────────────────────────────────────────────────────────────────

auto cmd_doctor() -> int {
    struct Check {
        std::string name, why;
        bool critical;
        std::optional<std::string> version; // nullopt: missing
    };
    auto version_of = [](const std::vector<std::string>& cmd, auto pattern_fn) -> std::optional<std::string> {
        const Result r = run(cmd, {}, 5);
        if (!r.started) {
            return std::nullopt;
        }
        return pattern_fn(r.out + r.err);
    };
    auto first_number = [](const std::string& text) -> std::optional<std::string> {
        if (const auto m = ctre::search<R"RE((\d+\.\d+(?:\.\d+)*))RE">(text)) {
            return m.get<1>().to_string();
        }
        return "found";
    };

    std::vector<Check> checks;
    checks.push_back({.name = "cmake", .why = "Build system (3.20+)", .critical = true, .version = cmake_version()});

    std::string cxx = cxx_compiler();
    std::optional<std::string> cxx_ok;
    if (!cxx.empty() && which(cxx)) {
        const fs::path src = fs::temp_directory_path() / "revet-doctor-cxx23.cpp";
        write_file(src, "#include <utility>\nauto f(auto x) { return x; }\nint main() { return f(0); }\n");
        const Result r = run({cxx, "-std=c++23", "-c", src.string(), "-o", "/dev/null"}, {}, 20);
        remove_quietly(src);
        cxx_ok = r.rc == 0 ? std::optional<std::string>(cxx) : std::nullopt;
    }
    checks.push_back({.name = "c++", .why = "C++23 compiler", .critical = true, .version = cxx_ok});
    checks.push_back({.name = "conan",
                      .why = "Package manager (curl, catch2, app deps)",
                      .critical = true,
                      .version = version_of({"conan", "--version"}, first_number)});
    const auto optional = std::to_array<std::pair<const char*, const char*>>({
        {"ninja", "Fast builds (fallback: make)"},
        {"sqlite3", "Database (optional feature)"},
        {"clang-tidy", "Linting (revet tidy)"},
        {"clang-format", "Formatting (revet format)"},
        {"ccache", "Build cache (-DREVET_CCACHE=ON)"},
        {"samply", "Profiling (revet dev --profile)"},
        {"lldb", "Debugger (revet dev --lldb, d key)"},
        {"bun", "Builds static/optimistic.ts"},
    });
    std::ranges::transform(optional, std::back_inserter(checks), [&](const auto& tool) -> Check {
        const auto& [name, why] = tool;
        return {.name = name,
                .why = why,
                .critical = false,
                .version = which(name) ? version_of({name, "--version"}, first_number) : std::nullopt};
    });

    info("");
    rule("Dependency Check");
    const auto missing = [](bool critical) -> auto {
        return [critical](const Check& c) -> bool { return !c.version && c.critical == critical; };
    };
    const auto critical_missing = std::ranges::count_if(checks, missing(true));
    const auto optional_missing = std::ranges::count_if(checks, missing(false));
    for (const auto& c : checks) {
        const bool found = c.version.has_value();
        std::string line = std::string(found ? "✓ " : "✗ ") + c.name;
        line.resize(std::max<size_t>(line.size(), 17), ' ');
        line += c.why;
        line.resize(std::max<size_t>(line.size(), 60), ' ');
        if (found) {
            line += " " + *c.version;
        }
        if (!found && c.critical) {
            line += " [CRITICAL]";
        }
        info(line);
    }
    if (critical_missing != 0) {
        err(std::to_string(critical_missing) + " critical missing. Run: bash " + install_deps_script().string());
        return 1;
    }
    if (optional_missing != 0) {
        dim(std::to_string(optional_missing) + " optional missing (dev/profiling only)");
    }
    return 0;
}

} // namespace revet::cli
