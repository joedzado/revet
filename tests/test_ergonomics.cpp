#include <catch2/catch_test_macros.hpp>
#include <revet/html.hpp>
#include <revet/htmx.hpp>

TEST_CASE("Ergonomics — #id shorthand", "[html]") {
    const std::string s = html::div{" #main-content", "Hello"};
    CHECK(s == "<div id=\"main-content\">Hello</div>");
}

TEST_CASE("Ergonomics — .class shorthand", "[html]") {
    const std::string s = html::div{" .container", " .fluid", "Content"};
    CHECK(s == "<div class=\"container fluid\">Content</div>");
}

TEST_CASE("Ergonomics — mixed id and class shorthand", "[html]") {
    const std::string s = html::div{" #hero", " .big", " .bold", "Welcome"};
    CHECK(s == "<div id=\"hero\" class=\"big bold\">Welcome</div>");
}

TEST_CASE("Ergonomics — attr member functions", "[html]") {
    const std::string s =
        html::input{html::attr.type("text"), html::attr.name("search"), html::attr.placeholder("Search...")};
    CHECK(s == "<input type=\"text\" name=\"search\" placeholder=\"Search...\">");
}

TEST_CASE("Ergonomics — boolean attributes", "[html]") {
    const std::string s = html::input{html::attr.type("checkbox"), html::attr.checked(), html::attr.required()};
    CHECK(s == "<input type=\"checkbox\" checked required>");
}

TEST_CASE("Ergonomics — event attributes", "[html]") {
    const std::string s = html::button{html::attr.onclick("alert('hi')"), "Click Me"};
    CHECK(s == "<button onclick=\"alert(&#39;hi&#39;)\">Click Me</button>");
}

TEST_CASE("Ergonomics — HTMX new attributes", "[htmx]") {
    const std::string s = html::div{htmx::attr::swap_oob(), htmx::attr::on("click", "alert('htmx')"), "HTMX Content"};
    CHECK(s == "<div hx-swap-oob=\"true\" hx-on:click=\"alert(&#39;htmx&#39;)\">HTMX Content</div>");
}
