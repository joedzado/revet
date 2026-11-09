#pragma once
// The <head> every Revet page repeats: charset, viewport, title, theme restore, styles, htmx.
//
//   html::html_doc{html::lang("en"),
//       html::standard_head({.title = title, .theme_color = "#111111"}, {scripts::chart_tag()}),
//       html::body{...},
//   }

#include "css.hpp"
#include "htmx.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

namespace html {

struct HeadOptions {
    std::string_view title;
    std::string_view description = {};  // <meta name="description">
    std::string_view theme_color = {};  // <meta name="theme-color">
    std::string_view viewport = "width=device-width,initial-scale=1";
    std::string_view icon = {};         // <link rel="icon" href=...>
    // Applies a saved data-theme before first paint, so a dark-mode page doesn't flash light.
    bool theme_restore = true;
    bool htmx = true;
    bool optimistic = false;            // needed for htmx::optimistic_* and htmx::js::*
    std::string_view htmx_config = {};  // JSON for <meta name="htmx-config">
};

inline auto standard_head(const HeadOptions& o, std::initializer_list<Node> extra = {}) -> Node {
    std::string out = "<head>";
    out += meta{attr.charset("utf-8")}.str;
    out += meta{attr.name("viewport"), attr.content(o.viewport)}.str;
    out += title{o.title}.str;
    if (!o.description.empty()) {
        out += meta{attr.name("description"), attr.content(o.description)}.str;
    }
    if (!o.theme_color.empty()) {
        out += meta{attr.name("theme-color"), attr.content(o.theme_color)}.str;
    }
    if (!o.icon.empty()) {
        out += link{attr.rel("icon"), attr.href(o.icon)}.str;
    }
    if (!o.htmx_config.empty()) {
        out += meta{attr.name("htmx-config"), attr.content(o.htmx_config)}.str;
    }
    if (o.theme_restore) {
        out += script{raw("(function(){try{var t=localStorage.getItem('theme');"
                          "if(t)document.documentElement.dataset.theme=t;}catch(e){}})()")}
                   .str;
    }
    out += collect_styles().str;
    if (o.htmx) {
        out += htmx::script_tag().str;
    }
    if (o.optimistic) {
        out += htmx::optimistic_script_tag().str;
    }
    for (const auto& n : extra) {
        out += n.str;
    }
    out += "</head>";
    return raw(std::move(out));
}

} // namespace html
