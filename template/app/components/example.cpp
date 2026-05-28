#include <revet/web.hpp>

[[revet::export]]
html::Node badge(html::Node content) {
    static const auto style = html::css_class({
        .padding       = "0.2rem 0.6rem",
        .border        = "1px solid var(--accent-border)",
        .border_radius = css::radius::full,
        .background    = "var(--accent-dim)",
        .color         = "var(--accent)",
        .font_family   = "var(--font-mono)",
        .font_size     = "0.6875rem",
        .font_weight   = css::weight::medium,
    });
    return html::span{ style, std::move(content) };
}
