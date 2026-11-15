// Auto-generated — do not edit
#pragma once
#include <revet/html.hpp>
#include <string_view>

namespace scripts {
inline constexpr std::string_view _9_lives_min = "/_app/9-lives.min.f2acf862b5.js";
inline constexpr std::string_view _9_lives_min_js = "// second\nlet q = 1;\n";
inline auto _9_lives_min_tag() -> html::Node {
    return html::script{html::attr.src(_9_lives_min), html::attr("defer", "")};
}
inline constexpr std::string_view chart = "/_app/chart.85c991bf8f.js";
inline constexpr std::string_view chart_js = "export function a() { return \"x\\\\y\"; }\n";
inline auto chart_tag() -> html::Node {
    return html::script{html::attr.src(chart), html::attr("defer", "")};
}
} // namespace scripts
