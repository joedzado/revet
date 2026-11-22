// revet CLI helpers: revet.toml editing, Conan bookkeeping, route table, processes, files, hashes.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <unistd.h>

#include "cli.hpp"

using namespace revet::cli; // NOLINT(google-build-using-namespace)

namespace {
constexpr std::string_view k_toml = "name = \"x\"\n\n[build.dependencies]\nfmt = \"fmt/10.1.0\"\n\"zlib\" = "
                                    "\"zlib/1.3\"\nspdlog\n\n[htmx]\nversion = \"2.0.9\"\n";
}

TEST_CASE("deps_from_toml reads [build.dependencies] in order") {
    Deps d = deps_from_toml(k_toml);
    REQUIRE(d.size() == 2);
    CHECK(d.at(0) == std::pair<std::string, std::string>{"fmt", "fmt/10.1.0"});
    CHECK(d.at(1) == std::pair<std::string, std::string>{"zlib", "zlib/1.3"});
    CHECK(deps_from_toml("name = \"x\"\n").empty());
    // A bare `name =` uses the name as the reference.
    CHECK(deps_from_toml("[build.dependencies]\nboost =\n").at(0).second == "boost");
}

TEST_CASE("deps_hash: stable, order-independent, changes with the deps") {
    const Deps a = {{"fmt", "fmt/1"}, {"zlib", "zlib/1.3"}};
    const Deps b = {{"zlib", "zlib/1.3"}, {"fmt", "fmt/1"}};
    CHECK(deps_hash(a).size() == 8);
    CHECK(deps_hash(a) == deps_hash(b));
    CHECK(deps_hash(a) != deps_hash({{"fmt", "fmt/2"}, {"zlib", "zlib/1.3"}}));
    CHECK(deps_hash({}) != deps_hash(a));
}

TEST_CASE("upsert_dep replaces, appends or creates the section") {
    CHECK(upsert_dep(k_toml, "fmt", "fmt/11.0.0").contains("fmt = \"fmt/11.0.0\"\n\"zlib\""));
    const std::string added = upsert_dep(k_toml, "newdep", "newdep/1.0");
    CHECK(added.contains("spdlog\n\nnewdep = \"newdep/1.0\"\n[htmx]"));
    CHECK(upsert_dep("name = \"x\"", "fmt", "fmt/1") == "name = \"x\"\n\n[build.dependencies]\nfmt = \"fmt/1\"\n");
    // No trailing newline on the last dependency: the new one still gets its own line.
    CHECK(upsert_dep("[build.dependencies]\na = \"a/1\"", "b", "b/2") ==
          "[build.dependencies]\na = \"a/1\"\nb = \"b/2\"\n");
    // `fmt_extra` is not `fmt`.
    CHECK(upsert_dep("[build.dependencies]\nfmt_extra = \"e/1\"\n", "fmt", "fmt/2") ==
          "[build.dependencies]\nfmt_extra = \"e/1\"\nfmt = \"fmt/2\"\n");
}

TEST_CASE("remove_dep removes only inside [build.dependencies]") {
    auto [out, found] = remove_dep("[build.dependencies]\nfmt = \"fmt/1\"\n[other]\nfmt = \"x\"\n", "fmt");
    CHECK(found);
    CHECK(out == "[build.dependencies]\n[other]\nfmt = \"x\"\n");
    CHECK_FALSE(remove_dep(k_toml, "missing").second);
}

TEST_CASE("conan list output: refs and newest-first order") {
    const std::string list =
        "conancenter\n  fmt\n    fmt/9.1.0\n    fmt/10.0.0\n    fmt/10.2.1\n  fmtlog\n    fmtlog/2.2.1\n";
    auto refs = conan_refs(list, "fmt");
    sort_refs_newest_first(refs);
    CHECK(refs == std::vector<std::string>{"fmt/10.2.1", "fmt/10.0.0", "fmt/9.1.0"});
    CHECK(conan_refs(list).size() == 4);
    CHECK(ref_version("fmt/10.2.1") == "10.2.1");
}

TEST_CASE("parse_routes lists methods, async ones included") {
    const std::string src =
        "    {\n        web::Route r; r.path = \"/a\";\n        r.get = [](const web::Request& req) {};\n"
        "        r.async_post = [](const web::Request& req) {};\n        out.push_back(r);\n    }\n"
        "    {\n        web::Route r; r.path = \"/s\";\n        r.sse = [](auto&, auto&) {};\n        "
        "out.push_back(r);\n    }\n";
    auto routes = parse_routes(src);
    REQUIRE(routes.size() == 2);
    CHECK(routes.at(0).path == "/a");
    CHECK(routes.at(0).methods == std::vector<std::string>{"GET", "POST"});
    CHECK(routes.at(1).methods == std::vector<std::string>{"SSE"});
}

TEST_CASE("hashes") {
    CHECK(md5_hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5_hex("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6");
    CHECK(sha1_hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(sha1_hex("/tmp/some/profile.txt") == "9743d28ad354f1e0ff3a722ad27ad5cdafe22f36");
}

TEST_CASE("expand_tabs") {
    CHECK(expand_tabs("a\tb") == "a   b");
    CHECK(expand_tabs("\tx\n\ty") == "    x\n    y");
}

// ── More helpers ────────────────────────────────────────────────────────────

namespace {
// Fresh scratch directory, made current for the test's lifetime.
struct TempCwd {
    fs::path dir, prev;
    TempCwd() : prev(fs::current_path()) {
        std::string tmpl = (fs::temp_directory_path() / "revet-test-XXXXXX").string();
        dir = mkdtemp(tmpl.data());
        fs::current_path(dir);
    }
    TempCwd(const TempCwd&) = delete;
    TempCwd(TempCwd&&) = delete;
    auto operator=(const TempCwd&) -> TempCwd& = delete;
    auto operator=(TempCwd&&) -> TempCwd& = delete;
    ~TempCwd() {
        fs::current_path(prev);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};
} // namespace

TEST_CASE("strip, starts_with, split_lines_keepends") {
    CHECK(strip(" \t\r\n x y \v\f\n") == "x y");
    CHECK(strip("   ").empty());
    CHECK(starts_with("[build.dependencies]", "[build."));
    CHECK_FALSE(starts_with("[b", "[build"));
    CHECK(split_lines_keepends("a\nb\n") == std::vector<std::string>{"a\n", "b\n"});
    CHECK(split_lines_keepends("a\n\nb") == std::vector<std::string>{"a\n", "\n", "b"});
    CHECK(split_lines_keepends("").empty());
}

TEST_CASE("read_file / write_file round-trip and create parents") {
    const TempCwd t;
    write_file("a/b/c.txt", std::string("x\0y", 3));
    CHECK(read_file("a/b/c.txt") == std::string("x\0y", 3));
    write_file("a/b/c.txt", "short");
    CHECK(read_file("a/b/c.txt") == "short");
    CHECK_FALSE(read_file("missing").has_value());
}

TEST_CASE("toml_port and project_binary_name") {
    const TempCwd t;
    CHECK_FALSE(toml_port(t.dir).has_value());
    CHECK(project_binary_name(t.dir) == "revet");
    write_file("revet.toml", "name = \"x\"\n[server]\n  port = 9090\nbinary = \"my-app_2\"\n");
    CHECK(toml_port(t.dir) == 9090);
    CHECK(project_binary_name(t.dir) == "my-app_2");
    write_file("revet.toml", "name = \"x\"\n# port = 1\n");
    CHECK_FALSE(toml_port(t.dir).has_value());
}

TEST_CASE("deps_from_toml: quoting, duplicates, comments, end of section") {
    Deps d = deps_from_toml("[build.dependencies]\n"
                            "  'fmt' = \"fmt/1\"\n"
                            "# comment = \"no/1\"\n"
                            "fmt = \"fmt/2\"\n"
                            "with-dash = \"with-dash/0.1\"\n"
                            "[server]\nport = \"8080\"\n");
    REQUIRE(d.size() == 2);
    CHECK(d.at(0) == std::pair<std::string, std::string>{"fmt", "fmt/2"}); // later entry wins, first position kept
    CHECK(d.at(1).first == "with-dash");
    CHECK(find_dep(d, "fmt") != nullptr);
    CHECK(*find_dep(d, "with-dash") == "with-dash/0.1");
    CHECK(find_dep(d, "port") == nullptr);
}

TEST_CASE("upsert_dep / remove_dep: quoted keys and [build.*] subsections") {
    const std::string toml = "[build.dependencies]\n\"fmt\" = \"fmt/1\"\n[build.options]\nx = 1\n[server]\n";
    std::string up = upsert_dep(toml, "fmt", "fmt/2");
    CHECK(up == "[build.dependencies]\nfmt = \"fmt/2\"\n[build.options]\nx = 1\n[server]\n");
    // A new entry stays in [build.dependencies], where deps_from_toml reads it, even before a [build.*] table.
    std::string added = upsert_dep(toml, "zlib", "zlib/1.3");
    CHECK(added ==
          "[build.dependencies]\n\"fmt\" = \"fmt/1\"\nzlib = \"zlib/1.3\"\n[build.options]\nx = 1\n[server]\n");
    CHECK(find_dep(deps_from_toml(added), "zlib") != nullptr);
    auto [out, found] = remove_dep(toml, "fmt");
    CHECK(found);
    CHECK(out == "[build.dependencies]\n[build.options]\nx = 1\n[server]\n");
    // Round trip: add then remove leaves the deps unchanged.
    CHECK(deps_from_toml(remove_dep(upsert_dep(k_toml, "new", "new/1"), "new").first) == deps_from_toml(k_toml));
}

TEST_CASE("sort_refs_newest_first compares versions numerically") {
    std::vector<std::string> refs = {"a/1.9", "a/1.10", "a/1.10.1", "a/2", "a/0.9.99", "a/1.10-1"};
    sort_refs_newest_first(refs);
    CHECK(refs == std::vector<std::string>{"a/2", "a/1.10.1", "a/1.10-1", "a/1.10", "a/1.9", "a/0.9.99"});
    CHECK(ref_version("noslash") == "noslash");
    CHECK(conan_refs("Found 0 pkg/version recipes\n").empty());
}

TEST_CASE("framework requirements feed the conanfile and the deps hash") {
    auto reqs = framework_requirements();
    REQUIRE_FALSE(reqs.empty());
    CHECK(std::ranges::all_of(reqs, [](const std::string& r) -> bool { return r.contains('/'); }));

    const TempCwd t;
    CHECK(project_conanfile({}) == framework_dir() / "conanfile.txt");
    std::string first = reqs.at(0).substr(0, reqs.at(0).find('/'));
    const fs::path p = project_conanfile({{"fmt", "fmt/10.2.1"}, {first, first + "/0.0.1"}});
    const std::string content = read_file(p).value_or("");
    CHECK(content.starts_with("[requires]\n" + reqs.at(0) + "\n"));
    CHECK(content.contains("fmt/10.2.1\n"));
    CHECK_FALSE(content.contains(first + "/0.0.1")); // framework's version wins
    CHECK(content.ends_with("[generators]\nCMakeDeps\nCMakeToolchain\n"));
}

TEST_CASE("conan_needs_update follows the toolchain and the hash marker") {
    const TempCwd t;
    const Deps deps = {{"fmt", "fmt/1"}};
    const fs::path conan = "build/conan";
    CHECK(conan_needs_update(deps, conan));
    write_file(conan / "conan_toolchain.cmake", "");
    CHECK(conan_needs_update(deps, conan));
    conan_mark_updated(deps, conan);
    CHECK_FALSE(conan_needs_update(deps, conan));
    CHECK(conan_needs_update({{"fmt", "fmt/2"}}, conan));
    write_file(conan / ".revet_deps_hash", deps_hash(deps) + "\n"); // trailing newline is fine
    CHECK_FALSE(conan_needs_update(deps, conan));
    fs::remove(conan / "conan_toolchain.cmake");
    CHECK(conan_needs_update(deps, conan));
}

TEST_CASE("parse_routes: all methods, skips blocks without a path") {
    const std::string src =
        "    {\n        web::Route r; r.path = \"/x/{id}\";\n        r.put = f; r.del = f; r.async_patch "
        "= f; r.ws = f;\n"
        "        out.push_back(r);\n    }\n"
        "    {\n        web::Route r;\n        r.get = f;\n        out.push_back(r);\n    }\n";
    auto routes = parse_routes(src);
    REQUIRE(routes.size() == 1);
    CHECK(routes.at(0).path == "/x/{id}");
    CHECK(routes.at(0).methods == std::vector<std::string>{"PUT", "DEL", "PATCH", "WS"});
    CHECK(parse_routes("").empty());
}

TEST_CASE("run captures output, exit status, timeouts and missing programs") {
    Result r = run({"/bin/sh", "-c", "printf out; printf err >&2; exit 3"});
    CHECK(r.started);
    CHECK(r.rc == 3);
    CHECK(r.out == "out");
    CHECK(r.err == "err");

    const TempCwd t;
    CHECK(run({"/bin/pwd"}, t.dir).out.contains(t.dir.filename().string()));

    const Result slow = run({"/bin/sleep", "5"}, {}, 1);
    CHECK(slow.rc == -1);

    const Result missing = run({"revet-no-such-program"});
    CHECK_FALSE(missing.started);
    CHECK(missing.rc == -1);

    CHECK(run({"/bin/sh", "-c", "kill -9 $$"}).rc == 128 + 9);
    CHECK(run_inherit({"/usr/bin/true"}) == 0);
}

TEST_CASE("which") {
    CHECK(which("sh").has_value());
    CHECK(which("/bin/sh") == fs::path("/bin/sh"));
    CHECK_FALSE(which("revet-no-such-program").has_value());
    CHECK_FALSE(which("/no/such/sh").has_value());
}

TEST_CASE("framework_dir and version") {
    CHECK(fs::exists(framework_dir() / "template" / "CMakeLists.txt"));
    CHECK_FALSE(framework_version().empty());
    CHECK(framework_version() == strip(read_file(framework_dir() / "VERSION").value_or("")));
}

TEST_CASE("strip_pch drops precompiled-header flags only") {
    const std::string db =
        R"("command": "c++ -Wall -Xclang -include-pch -Xclang /b/cmake_pch.hxx.pch -Xclang -include -Xclang /b/cmake_pch.hxx -c a.cpp")";
    CHECK(strip_pch(db) == R"("command": "c++ -Wall -Xclang -include -Xclang /b/cmake_pch.hxx -c a.cpp")");
    CHECK(strip_pch("c++ -c a.cpp") == "c++ -c a.cpp");
}
