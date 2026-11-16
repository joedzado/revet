// revet CLI helpers: revet.toml editing, Conan bookkeeping, route table, hashes.
#include <catch2/catch_test_macros.hpp>

#include "cli.hpp"

using namespace revet::cli;

namespace {
const std::string kToml =
    "name = \"x\"\n\n[build.dependencies]\nfmt = \"fmt/10.1.0\"\n\"zlib\" = \"zlib/1.3\"\nspdlog\n\n[htmx]\nversion = \"2.0.9\"\n";
}

TEST_CASE("deps_from_toml reads [build.dependencies] in order") {
    Deps d = deps_from_toml(kToml);
    REQUIRE(d.size() == 2);
    CHECK(d[0] == std::pair<std::string, std::string>{"fmt", "fmt/10.1.0"});
    CHECK(d[1] == std::pair<std::string, std::string>{"zlib", "zlib/1.3"});
    CHECK(deps_from_toml("name = \"x\"\n").empty());
    // A bare `name =` uses the name as the reference.
    CHECK(deps_from_toml("[build.dependencies]\nboost =\n")[0].second == "boost");
}

TEST_CASE("deps_hash: stable, order-independent, changes with the deps") {
    Deps a = {{"fmt", "fmt/1"}, {"zlib", "zlib/1.3"}};
    Deps b = {{"zlib", "zlib/1.3"}, {"fmt", "fmt/1"}};
    CHECK(deps_hash(a).size() == 8);
    CHECK(deps_hash(a) == deps_hash(b));
    CHECK(deps_hash(a) != deps_hash({{"fmt", "fmt/2"}, {"zlib", "zlib/1.3"}}));
    CHECK(deps_hash({}) != deps_hash(a));
}

TEST_CASE("upsert_dep replaces, appends or creates the section") {
    CHECK(upsert_dep(kToml, "fmt", "fmt/11.0.0").find("fmt = \"fmt/11.0.0\"\n\"zlib\"") != std::string::npos);
    std::string added = upsert_dep(kToml, "newdep", "newdep/1.0");
    CHECK(added.find("spdlog\n\nnewdep = \"newdep/1.0\"\n[htmx]") != std::string::npos);
    CHECK(upsert_dep("name = \"x\"", "fmt", "fmt/1") == "name = \"x\"\n\n[build.dependencies]\nfmt = \"fmt/1\"\n");
    // No trailing newline on the last dependency: the new one still gets its own line.
    CHECK(upsert_dep("[build.dependencies]\na = \"a/1\"", "b", "b/2") == "[build.dependencies]\na = \"a/1\"\nb = \"b/2\"\n");
    // `fmt_extra` is not `fmt`.
    CHECK(upsert_dep("[build.dependencies]\nfmt_extra = \"e/1\"\n", "fmt", "fmt/2") ==
          "[build.dependencies]\nfmt_extra = \"e/1\"\nfmt = \"fmt/2\"\n");
}

TEST_CASE("remove_dep removes only inside [build.dependencies]") {
    auto [out, found] = remove_dep("[build.dependencies]\nfmt = \"fmt/1\"\n[other]\nfmt = \"x\"\n", "fmt");
    CHECK(found);
    CHECK(out == "[build.dependencies]\n[other]\nfmt = \"x\"\n");
    CHECK_FALSE(remove_dep(kToml, "missing").second);
}

TEST_CASE("conan list output: refs and newest-first order") {
    std::string list = "conancenter\n  fmt\n    fmt/9.1.0\n    fmt/10.0.0\n    fmt/10.2.1\n  fmtlog\n    fmtlog/2.2.1\n";
    auto refs = conan_refs(list, "fmt");
    sort_refs_newest_first(refs);
    CHECK(refs == std::vector<std::string>{"fmt/10.2.1", "fmt/10.0.0", "fmt/9.1.0"});
    CHECK(conan_refs(list).size() == 4);
    CHECK(ref_version("fmt/10.2.1") == "10.2.1");
}

TEST_CASE("parse_routes lists methods, async ones included") {
    std::string src =
        "    {\n        web::Route r; r.path = \"/a\";\n        r.get = [](const web::Request& req) {};\n"
        "        r.async_post = [](const web::Request& req) {};\n        out.push_back(r);\n    }\n"
        "    {\n        web::Route r; r.path = \"/s\";\n        r.sse = [](auto&, auto&) {};\n        out.push_back(r);\n    }\n";
    auto routes = parse_routes(src);
    REQUIRE(routes.size() == 2);
    CHECK(routes[0].path == "/a");
    CHECK(routes[0].methods == std::vector<std::string>{"GET", "POST"});
    CHECK(routes[1].methods == std::vector<std::string>{"SSE"});
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
