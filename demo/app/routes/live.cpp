#include "routes.hpp"
#include "styles.hpp"
#include <revet/htmx.hpp>
#include <revet/ui.hpp>

static const auto color_grid_style = css::flex(css::wrap::wrap, css::gap::_3)
    .extra("padding:1.5rem 0;min-height:160px");

static auto render_get(const auto& req) {
    auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_live_eyebrow},
            html::h1{i18n::page_live_h1},
            html::p{i18n::page_live_body},
        } +
        html::style{"@keyframes sw-in{from{opacity:0;transform:scale(.7)}to{opacity:1;transform:scale(1)}}"} +
        html::div{
            htmx::attr::ext("sse"),
            htmx::sse::connect("/live/stream"),
            html::div{
                html::attr.id("color-grid"),
                htmx::sse::swap("color"),
                htmx::attr::swap_before_end(),
                color_grid_style,
            },
        };

    return layout_page(req, i18n::page_live_h1, urls::live, content);
}
