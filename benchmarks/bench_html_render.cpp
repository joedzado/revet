#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <revet/html.hpp>

using namespace html;

// Typical single-attribute node
static auto make_simple_node() -> Node {
    return Node{"Hello, World!"};
}

// Typical form element with classes and attributes
static auto make_form_element() -> Node {
    return fragment(attr.type("text"), attr.name("email"), attr.placeholder("Enter email"),
                    raw(" .input .input-bordered .w-full"));
}

// Fragment with many children (simulates a list render)
static auto make_large_fragment(int n) -> Node {
    std::string result;
    result.reserve(n * 40);
    for (int i = 0; i < n; ++i) {
        result.append("<li class=\"item\">");
        result.append(std::format("Item {}", i));
        result.append("</li>");
    }
    return Node{raw_tag{}, std::move(result)};
}

// HTML-unsafe input (triggers escape)
static const std::string unsafe_content = "<script>alert('xss')</script> & \"quotes\" 'here'";

TEST_CASE("html render benchmarks", "[bench][html]") {
    BENCHMARK("Node from safe string (no escaping needed)") {
        return Node{"Hello World"};
    };

    BENCHMARK("Node from unsafe string (html_escape fires)") {
        return Node{unsafe_content};
    };

    BENCHMARK("attr() single attribute") {
        return attr("data-value", "hello-world");
    };

    BENCHMARK("attr() with unsafe value (triggers html_escape)") {
        return attr("data-value", "<script>alert(1)</script>");
    };

    BENCHMARK("fragment() 5 nodes") {
        return fragment(Node{"Hello"}, attr.id("main"), raw(" .container"), attr.type("text"), Node{"World"});
    };

    BENCHMARK("fragment() 20 nodes") {
        return fragment(Node{"a"}, Node{"b"}, Node{"c"}, Node{"d"}, Node{"e"}, Node{"f"}, Node{"g"}, Node{"h"},
                        Node{"i"}, Node{"j"}, Node{"k"}, Node{"l"}, Node{"m"}, Node{"n"}, Node{"o"}, Node{"p"},
                        Node{"q"}, Node{"r"}, Node{"s"}, Node{"t"});
    };

    BENCHMARK("node operator+ chain (5 nodes)") {
        return Node{"a"} + Node{"b"} + Node{"c"} + Node{"d"} + Node{"e"};
    };

    BENCHMARK("node operator+ chain (20 nodes)") {
        Node n;
        for (int i = 0; i < 20; ++i) {
            n = std::move(n) + Node{std::format("item{}", i)};
        }
        return n;
    };

    BENCHMARK("form element with 4 attrs + classes") {
        return make_form_element();
    };

    BENCHMARK("list render 10 items (pre-built string)") {
        return make_large_fragment(10);
    };

    BENCHMARK("list render 100 items (pre-built string)") {
        return make_large_fragment(100);
    };
}
