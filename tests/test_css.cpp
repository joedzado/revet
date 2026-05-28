#include <catch2/catch_test_macros.hpp>
#include <revet/css.hpp>
#include <revet/html.hpp>

// ── css_props ─────────────────────────────────────────────────────────────────

TEST_CASE("css_props — empty produces empty string", "[css]") {
    const html::css_props p{};
    CHECK(p.to_string().empty());
}

TEST_CASE("css_props — single property", "[css]") {
    const html::css_props p{.color = "red"};
    CHECK(p.to_string() == "color:red;");
}

TEST_CASE("css_props — multiple properties", "[css]") {
    const html::css_props p{.display = "flex", .gap = "1rem"};
    CHECK(p.to_string() == "display:flex;gap:1rem;");
}

TEST_CASE("css_props — complex properties", "[css]") {
    const html::css_props p{.margin = "0 auto", .padding = "1rem"};
    CHECK(p.to_string() == "margin:0 auto;padding:1rem;");
}

TEST_CASE("css_props — font properties", "[css]") {
    const html::css_props p{.color = "#333", .font_size = "1rem", .font_weight = "bold"};
    CHECK(p.to_string() == "color:#333;font-size:1rem;font-weight:bold;");
}

TEST_CASE("css_props — overwrite property", "[css]") {
    html::css_props p{.color = "red"};
    p.color = "blue";
    CHECK(p.to_string() == "color:blue;");
}

// ── css_class ─────────────────────────────────────────────────────────────────

TEST_CASE("css_class — from raw string", "[css]") {
    const html::css_class c{"color:red;"};
    CHECK(!c.name.empty());
    CHECK(c.name.starts_with("cw-"));
}

TEST_CASE("css_class — from css_props", "[css]") {
    const html::css_class c{html::css_props{.color = "blue"}};
    CHECK(!c.name.empty());
}

TEST_CASE("css_class — deduplication", "[css]") {
    const html::css_class c1{"color:red;"};
    const html::css_class c2{"color:red;"};
    CHECK(c1.name == c2.name);
}

TEST_CASE("css_class — uniqueness", "[css]") {
    const html::css_class c1{"color:red;"};
    const html::css_class c2{"color:blue;"};
    CHECK(c1.name != c2.name);
}

TEST_CASE("css_class — builder pattern", "[css]") {
    const html::css_class c{"color:red;"};
    const auto ch = c.hover(html::css_props{.color = "blue"});
    CHECK(ch.name == c.name);
    CHECK(ch.pseudo.size() == 1);
}

// ── html_doc styling ──────────────────────────────────────────────────────────

TEST_CASE("html_doc — injects styles", "[css]") {
    const html::css_class c{"color:red;"};
    const std::string out = html::html_doc{html::body{html::div{c, "hello"}}};

    CHECK(out.find("<style>") != std::string::npos);
    CHECK(out.find(".cw-") != std::string::npos);
    CHECK(out.find("color:red;") != std::string::npos);
}

TEST_CASE("html_doc — multiple classes", "[css]") {
    const html::css_class c1{"color:red;"};
    const html::css_class c2{"font-weight:bold;"};
    const std::string out = html::html_doc{html::body{html::div{c1, c2, "hello"}}};

    CHECK(out.find("color:red;") != std::string::npos);
    CHECK(out.find("font-weight:bold;") != std::string::npos);
}

TEST_CASE("html_doc — pseudo selectors", "[css]") {
    const html::css_class c = html::css_class{"color:red;"}.hover(html::css_props{.color = "blue"});
    const std::string out = html::html_doc{html::body{html::div{c, "hello"}}};

    CHECK(out.find(":hover") != std::string::npos);
    CHECK(out.find("color:blue;") != std::string::npos);
}
