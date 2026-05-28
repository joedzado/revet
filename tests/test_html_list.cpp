#include <array>
#include <catch2/catch_test_macros.hpp>
#include <revet/html.hpp>
#include <string>
#include <vector>

TEST_CASE("html::list renders vector of strings as elements", "[list]") {
    std::vector<std::string> items = {"apple", "banana", "cherry"};
    auto result = std::string(html::list(items, [](const auto& s) { return html::li{s}; }));
    CHECK(result == "<li>apple</li><li>banana</li><li>cherry</li>");
}

TEST_CASE("html::list with empty range returns empty Node", "[list]") {
    std::vector<std::string> empty;
    auto result = std::string(html::list(empty, [](const auto& s) { return html::li{s}; }));
    CHECK(result == "");
}

TEST_CASE("html::list is composable as child of ul", "[list]") {
    std::vector<std::string> items = {"first", "second"};
    auto result = std::string(html::ul{html::list(items, [](const auto& s) { return html::li{s}; })});
    CHECK(result == "<ul><li>first</li><li>second</li></ul>");
}

TEST_CASE("html::list escapes content in elements", "[list]") {
    std::vector<std::string> items = {"<script>alert('xss')</script>"};
    auto result = std::string(html::list(items, [](const auto& s) { return html::li{s}; }));
    CHECK(result == "<li>&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;</li>");
}

TEST_CASE("html::list works with struct data", "[list]") {
    struct Item {
        std::string name;
        int count;
    };
    std::vector<Item> items = {{"widget", 2}, {"gadget", 5}, {"doohickey", 1}};
    auto result = std::string(html::list(items, [](const auto& item) { return html::li{item.name}; }));
    CHECK(result == "<li>widget</li><li>gadget</li><li>doohickey</li>");
}

TEST_CASE("html::list with lambda returning std::string", "[list]") {
    std::vector<int> numbers = {1, 2, 3};
    auto result = std::string(
        html::list(numbers, [](const auto& n) { return std::string("<li>") + std::to_string(n) + "</li>"; }));
    CHECK(result == "<li>1</li><li>2</li><li>3</li>");
}

TEST_CASE("html::list with std::array", "[list]") {
    std::array<std::string, 3> items = {"a", "b", "c"};
    auto result = std::string(html::list(items, [](const auto& s) { return html::li{s}; }));
    CHECK(result == "<li>a</li><li>b</li><li>c</li>");
}

TEST_CASE("html::list with attributes in elements", "[list]") {
    std::vector<std::string> ids = {"item1", "item2"};
    auto result = std::string(html::list(ids, [](const auto& id) { return html::li{html::attr.id(id), "Content"}; }));
    CHECK(result == "<li id=\"item1\">Content</li><li id=\"item2\">Content</li>");
}

TEST_CASE("html::list with nested elements", "[list]") {
    std::vector<std::string> categories = {"Fruits", "Vegetables"};
    auto result =
        std::string(html::ul{html::list(categories, [](const auto& cat) { return html::li{html::strong{cat}}; })});
    CHECK(result == "<ul><li><strong>Fruits</strong></li><li><strong>Vegetables</strong></li></ul>");
}

TEST_CASE("html::list<Element> template version", "[list]") {
    std::vector<std::string> items = {"one", "two", "three"};
    auto result = std::string(html::list<html::li>(items));
    CHECK(result == "<li>one</li><li>two</li><li>three</li>");
}

TEST_CASE("html::list<Element> in ul", "[list]") {
    std::vector<std::string> items = {"apple", "banana"};
    auto result = std::string(html::ul{html::list<html::li>(items)});
    CHECK(result == "<ul><li>apple</li><li>banana</li></ul>");
}

TEST_CASE("html::list<Element> escapes content", "[list]") {
    std::vector<std::string> items = {"<span>tag</span>"};
    auto result = std::string(html::list<html::li>(items));
    CHECK(result == "<li>&lt;span&gt;tag&lt;/span&gt;</li>");
}

TEST_CASE("html::list<Element> with empty range", "[list]") {
    std::vector<std::string> empty;
    auto result = std::string(html::list<html::li>(empty));
    CHECK(result == "");
}

TEST_CASE("html::classes single class", "[classes]") {
    auto result = std::string(html::classes("button"));
    CHECK(result == " .button");
}

TEST_CASE("html::classes multiple classes", "[classes]") {
    auto result = std::string(html::classes("btn", "primary", "large"));
    CHECK(result == " .btn primary large");
}

TEST_CASE("html::classes filters empty strings", "[classes]") {
    auto result = std::string(html::classes("base", "", "active", "", "highlight"));
    CHECK(result == " .base active highlight");
}

TEST_CASE("html::classes with conditional strings", "[classes]") {
    bool is_admin = true;
    bool is_disabled = false;
    auto result =
        std::string(html::classes("btn", is_admin ? "admin-badge" : "", is_disabled ? "disabled" : "", "clickable"));
    CHECK(result == " .btn admin-badge clickable");
}

TEST_CASE("html::classes as child in div", "[classes]") {
    auto result = std::string(html::div{html::classes("card", "shadow"), "Content"});
    CHECK(result == "<div class=\"card shadow\">Content</div>");
}

TEST_CASE("html::classes combined with other attributes", "[classes]") {
    auto result = std::string(html::div{html::attr.id("main"), html::classes("container", "responsive"),
                                        html::attr.onclick("handler()"), "Body"});
    CHECK(result == "<div class=\"container responsive\" id=\"main\" onclick=\"handler()\">Body</div>");
}

TEST_CASE("html::classes all empty yields no class attribute", "[classes]") {
    auto result = std::string(html::div{html::classes("", "", ""), "Content"});
    CHECK(result == "<div>Content</div>");
}
