#include <catch2/catch_test_macros.hpp>
#include <revet/html.hpp>

TEST_CASE("Node — text is escaped", "[html]") {
    const html::Node n{"<b>bold</b>"};
    CHECK(n.str == "&lt;b&gt;bold&lt;/b&gt;");
}

TEST_CASE("Node — string_view ctor escapes", "[html]") {
    const std::string_view sv = "a & b";
    const html::Node n{sv};
    CHECK(n.str == "a &amp; b");
}

TEST_CASE("Node — std::string ctor escapes", "[html]") {
    const std::string s = "\"quoted\"";
    const html::Node n{s};
    CHECK(n.str == "&quot;quoted&quot;");
}

TEST_CASE("Node — raw_tag skips escaping", "[html]") {
    const html::Node n{html::raw_tag{}, "<b>bold</b>"};
    CHECK(n.str == "<b>bold</b>");
}

TEST_CASE("Node — empty string", "[html]") {
    const html::Node n{""};
    CHECK(n.str.empty());
}

TEST_CASE("raw() — passes through unescaped", "[html]") {
    const auto n = html::raw("<svg><circle/></svg>");
    CHECK(n.str == "<svg><circle/></svg>");
}

TEST_CASE("attr() — escapes value", "[html]") {
    const auto n = html::attr("href", "/?q=<script>");
    CHECK(n.str == " href=\"/?q=&lt;script&gt;\"");
}

TEST_CASE("attr() — escapes quotes in value", "[html]") {
    const auto n = html::attr("title", "say \"hello\"");
    CHECK(n.str == " title=\"say &quot;hello&quot;\"");
}

TEST_CASE("attr() — empty value", "[html]") {
    const auto n = html::attr("disabled", "");
    CHECK(n.str == " disabled=\"\"");
}

TEST_CASE("elem — simple text", "[html]") {
    const std::string s = html::p{"hello"};
    CHECK(s == "<p>hello</p>");
}

TEST_CASE("elem — text is escaped via const char*", "[html]") {
    const std::string s = html::p{"<b>bold</b>"};
    CHECK(s == "<p>&lt;b&gt;bold&lt;/b&gt;</p>");
}

TEST_CASE("elem — with attr", "[html]") {
    const std::string s = html::div{html::attr("id", "main"), html::p{"hi"}};
    CHECK(s == "<div id=\"main\"><p>hi</p></div>");
}

TEST_CASE("elem — multiple attrs", "[html]") {
    const std::string s = html::input{html::attr("type", "text"), html::attr("name", "q")};
    CHECK(s == "<input type=\"text\" name=\"q\">");
}

TEST_CASE("elem — nesting", "[html]") {
    const std::string s = html::ul{
        html::li{"one"},
        html::li{"two"},
        html::li{"three"},
    };
    CHECK(s == "<ul><li>one</li><li>two</li><li>three</li></ul>");
}

TEST_CASE("elem — deep nesting", "[html]") {
    const std::string s = html::div{html::div{html::span{"inner"}}};
    CHECK(s == "<div><div><span>inner</span></div></div>");
}

TEST_CASE("void_elem — no children", "[html]") {
    const std::string s = html::br{};
    CHECK(s == "<br>");
}

TEST_CASE("void_elem — with attrs", "[html]") {
    const std::string s = html::input{html::attr("type", "submit"), html::attr("value", "Go")};
    CHECK(s == "<input type=\"submit\" value=\"Go\">");
}

TEST_CASE("void_elem — attr value escaped", "[html]") {
    const std::string s = html::input{html::attr("value", "<script>")};
    CHECK(s == "<input value=\"&lt;script&gt;\">");
}

TEST_CASE("elem — raw() child not escaped", "[html]") {
    const std::string s = html::div{html::raw("<b>bold</b>")};
    CHECK(s == "<div><b>bold</b></div>");
}

TEST_CASE("elem — mixed raw and text", "[html]") {
    const std::string s = html::div{
        html::raw("<em>em</em>"),
        html::span{"<safe>"},
    };
    CHECK(s == "<div><em>em</em><span>&lt;safe&gt;</span></div>");
}

TEST_CASE("elem — operator std::string lvalue", "[html]") {
    const html::p node{"hello"};
    const std::string s = static_cast<std::string>(node); // lvalue conversion
    CHECK(s == "<p>hello</p>");
}

TEST_CASE("elem — single string constructor (const char* shorthand)", "[html]") {
    // elem(const char* s) — single text child, no initializer_list
    html::h1 h{"Page Title"};
    const std::string s = std::move(h);
    CHECK(s == "<h1>Page Title</h1>");
}

TEST_CASE("head/body structure", "[html]") {
    const std::string s = html::head{html::title{"My Page"}};
    CHECK(s == "<head><title>My Page</title></head>");
}

TEST_CASE("table structure", "[html]") {
    const std::string s = html::table{
        html::tr{
            html::th{"Name"},
            html::th{"Age"},
        },
        html::tr{
            html::td{"Alice"},
            html::td{"30"},
        },
    };
    CHECK(s == "<table><tr><th>Name</th><th>Age</th></tr><tr><td>Alice</td><td>30</td></tr></table>");
}

TEST_CASE("form structure", "[html]") {
    const std::string s = html::form{
        html::attr("method", "get"),
        html::input{html::attr("type", "text"), html::attr("name", "q")},
        html::button{"Search"},
    };
    CHECK(s == "<form method=\"get\"><input type=\"text\" name=\"q\"><button>Search</button></form>");
}

TEST_CASE("text starting with a space cannot become an attribute", "[html][security]") {
    const std::string hostile = " onmouseover=alert(1) x";
    const std::string out = html::td{hostile};
    CHECK(out == "<td>&#32;onmouseover=alert(1) x</td>");
    CHECK(std::string(html::td{std::string(" .evil")}) == "<td>&#32;.evil</td>");
    CHECK(std::string(html::td{std::string(" #evil")}) == "<td>&#32;#evil</td>");
    CHECK(std::string(html::td{html::attr.title("ok"), "plain"}) == "<td title=\"ok\">plain</td>");
    CHECK(std::string(html::span{"a ", html::span{" b"}}) == "<span>a <span>&#32;b</span></span>");
}
