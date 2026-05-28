#include "routes.hpp"
#include <algorithm>
#include <string>

static const auto s_result_row   = html::css_class({.padding = ".45rem .875rem", .font_size = ".875rem"})
                                        .extra("border-bottom:1px solid var(--border)");
static const auto s_no_match     = html::css_class({.padding = ".5rem 0", .color = "var(--muted)", .font_size = ".875rem"});
static const auto s_results_wrap = html::css_class({
    .margin_top = ".5rem",
    .overflow = "hidden",
    .border = "1px solid var(--border)", .border_radius = "var(--r)",
    .background = "var(--surface)",
});

static constexpr std::array COUNTRIES = {
    "Afghanistan", "Albania", "Algeria", "Argentina", "Australia",
    "Austria", "Bangladesh", "Belgium", "Bolivia", "Brazil", "Cambodia",
    "Canada", "Chile", "China", "Colombia", "Croatia", "Czech Republic",
    "Denmark", "Ecuador", "Egypt", "Estonia", "Ethiopia", "Finland",
    "France", "Germany", "Ghana", "Greece", "Hungary", "India",
    "Indonesia", "Iran", "Iraq", "Ireland", "Israel", "Italy",
    "Japan", "Jordan", "Kenya", "Latvia", "Lithuania", "Malaysia",
    "Mexico", "Morocco", "Netherlands", "New Zealand", "Nigeria",
    "Norway", "Pakistan", "Peru", "Philippines", "Poland", "Portugal",
    "Romania", "Russia", "Saudi Arabia", "Singapore", "Slovakia",
    "Slovenia", "South Africa", "South Korea", "Spain", "Sweden",
    "Switzerland", "Thailand", "Turkey", "Ukraine", "United Arab Emirates",
    "United Kingdom", "United States", "Uruguay", "Venezuela", "Vietnam",
};

static auto render_get(const auto& req) -> web::Response {
    const auto q = std::string(req.query.get("q"));
    if (q.empty())
        return web::to_response(html::Node{});

    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };
    const auto ql = lower(q);

    html::Node rows;
    int count = 0;
    for (auto name : COUNTRIES) {
        if (lower(std::string(name)).find(ql) != std::string::npos) {
            rows = std::move(rows) + html::div{s_result_row, name};
            if (++count == 8) break;
        }
    }

    if (!count)
        return web::to_response(html::p{s_no_match, "No matches."});

    return web::to_response(html::div{s_results_wrap, std::move(rows)});
}
