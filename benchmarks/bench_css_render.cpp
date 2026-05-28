#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <revet/css.hpp>
#include <revet/css_utils.hpp>

// Simulates building CSS classes like a typical page would.
// Each css_class construction hashes the rule string and registers to thread-local.

static void register_n_classes(int n) {
    for (int i = 0; i < n; ++i) {
        html::css_class c(std::format("color:red;font-size:{}px;margin:{}px;", i * 2, i));
        [[maybe_unused]] html::Node node = c;
    }
}

TEST_CASE("css benchmarks", "[bench][css]") {
    BENCHMARK("css_class from raw string (short rules)") {
        return html::css_class{"display:flex;gap:8px;"};
    };

    BENCHMARK("css_class from raw string (long rules, 10 props)") {
        return html::css_class{"display:flex;flex-direction:column;gap:8px;padding:16px;"
                               "margin:0 auto;max-width:1200px;width:100%;"
                               "background-color:#fff;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);"};
    };

    BENCHMARK("css_class from css_props (5 fields set)") {
        html::css_props p;
        p.display = "flex";
        p.gap = "8px";
        p.padding = "16px";
        p.margin = "0 auto";
        p.width = "100%";
        return html::css_class{p};
    };

    BENCHMARK("css_props::to_string() (5 fields set)") {
        html::css_props p;
        p.display = "flex";
        p.gap = "8px";
        p.padding = "16px";
        p.margin = "0 auto";
        p.width = "100%";
        return p.to_string();
    };

    BENCHMARK("css_props::to_string() (20 fields set)") {
        html::css_props p;
        p.display = "flex";
        p.flex_direction = "column";
        p.gap = "8px";
        p.padding = "16px";
        p.margin = "0 auto";
        p.width = "100%";
        p.max_width = "1200px";
        p.background_color = "#fff";
        p.border_radius = "8px";
        p.color = "#333";
        p.font_size = "16px";
        p.font_weight = "400";
        p.line_height = "1.5";
        p.overflow = "hidden";
        p.position = "relative";
        p.top = "0";
        p.z_index = "1";
        p.cursor = "pointer";
        p.opacity = "1";
        p.transition = "all 0.2s";
        return p.to_string();
    };

    BENCHMARK("css_class operator Node() — registers to thread_local") {
        html::css_class c{"display:flex;gap:8px;"};
        return static_cast<html::Node>(c);
    };

    BENCHMARK("register 10 css_classes (simulates small page)") {
        register_n_classes(10);
    };

    BENCHMARK("register 50 css_classes (simulates medium page)") {
        register_n_classes(50);
    };

    BENCHMARK("html_doc::render() with 10 registered classes") {
        register_n_classes(10);
        html::detail::all_css_block = "body{margin:0;font-family:sans-serif;}";
        html::html_doc doc({html::raw("<head><title>Test</title>"), html::collect_styles(),
                            html::raw("</head><body><h1>Hello</h1></body>")});
        return std::string(doc);
    };

    BENCHMARK("html_doc::render() with 50 registered classes") {
        register_n_classes(50);
        html::detail::all_css_block = "body{margin:0;font-family:sans-serif;}";
        html::html_doc doc({html::raw("<head><title>Test</title>"), html::collect_styles(),
                            html::raw("</head><body><h1>Hello</h1></body>")});
        return std::string(doc);
    };

    BENCHMARK("detail::make_class_name (hash short rule)") {
        return html::detail::make_class_name("display:flex;gap:8px;");
    };

    BENCHMARK("detail::make_class_name (hash long rule)") {
        return html::detail::make_class_name("display:flex;flex-direction:column;gap:8px;padding:16px;"
                                             "margin:0 auto;max-width:1200px;width:100%;background-color:#fff;");
    };
}
