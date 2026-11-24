#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <revet/web.hpp>
#include <string>

namespace {

// A public directory with one file, and a secret next to it that must stay out of reach.
struct Tree {
    std::filesystem::path base = std::filesystem::temp_directory_path() / "revet-serve-file-test";
    Tree() {
        std::filesystem::remove_all(base);
        std::filesystem::create_directories(base / "public" / "fonts");
        std::ofstream(base / "public" / "fonts" / "a.txt") << "public";
        std::ofstream(base / "secret.txt") << "secret";
        web::set_public_root(base / "public");
    }
    ~Tree() {
        std::filesystem::remove_all(base);
        web::set_public_root({});
    }
    Tree(const Tree&) = delete;
    auto operator=(const Tree&) -> Tree& = delete;
    Tree(Tree&&) = delete;
    auto operator=(Tree&&) -> Tree& = delete;
};

} // namespace

TEST_CASE("serve_file: a file inside the public root is served", "[static]") {
    const Tree t;
    const auto r = web::serve_file("fonts/a.txt");
    REQUIRE(r.status.code == 200);
    REQUIRE(r.body == "public");
}

TEST_CASE("serve_file: nothing outside the public root is reachable", "[static]") {
    const Tree t;
    // What a decoded route param can look like.
    for (const std::string bad : std::initializer_list<std::string>{
             "../secret.txt", "fonts/../../secret.txt", "..", "fonts/../../../etc/passwd",
             (t.base / "secret.txt").string(), "/etc/passwd", ""}) {
        const auto r = web::serve_file(bad);
        INFO(bad);
        REQUIRE(r.status.code == 404);
        REQUIRE(r.body == "Not Found");
    }
}
