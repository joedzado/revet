#include "nav_icons.hpp"
#include "routes.hpp"
#include "styles.hpp"
#include "toggles.hpp"
#include "i18n.hpp"
#include <revet/csrf.hpp>
#include <revet/htmx.hpp>
#include <revet/ui.hpp>

static constexpr std::string_view HTMX_JS           = "https://unpkg.com/htmx.org@2.0.4/dist/htmx.min.js";
static constexpr std::string_view HTMX_SSE_JS       = "https://unpkg.com/htmx-ext-sse@2.2.2/sse.js";

// Not in CSS — internal scope key for ui::theme_toggle
static constexpr std::string_view THEME_SCOPE    = "layout";
static constexpr std::string_view BADGE_SSE      = "sse";
static constexpr std::string_view BADGE_JS       = "js";

// Brand strings
static constexpr std::string_view BRAND_NAME    = "revet";
static constexpr std::string_view BRAND_TAGLINE = "C++23 · server-rendered · type-safe";

static auto make_head(std::string_view title) -> html::Node {
    return html::head{
        html::meta{html::attr.charset("utf-8")},
        html::meta{html::attr.name("viewport"), html::attr.content("width=device-width,initial-scale=1")},
        html::meta{html::attr.name("color-scheme"), html::attr.content("light dark")},
        html::title{std::string(title) + " — revet"},
        html::collect_styles(),
        ui::smooth_scroll(),
        ui::reduced_motion(),
        html::script{html::attr.src(HTMX_JS)},
        html::script{html::attr.src(HTMX_SSE_JS)},
        htmx::optimistic_script_tag(),
    };
}

static auto nav_link(std::string_view href, html::Node icon,
                     std::string_view label, std::string_view active,
                     std::string_view badge = "") -> html::Node {
    const auto on = (href == active);
    return html::a{
        html::attr.href(href),
        styles::nav_link,
        on ? html::Node(styles::on) : html::Node{},
        std::move(icon),
        label,
        badge.empty() ? html::Node{} : html::span{styles::nav_badge, badge},
    };
}

static auto make_sidebar(std::string_view active) -> html::Node {
    return html::aside{
        ids::sidebar,
        html::div{styles::sb_brand,
            html::div{styles::sb_name, BRAND_NAME},
            html::div{styles::sb_sub, BRAND_TAGLINE},
        },
        html::nav{styles::sb_nav,
            html::div{styles::sb_section, i18n::nav_section_pages},
            nav_link(urls::index,      ico_home(),        i18n::nav_home,        active),
            nav_link(urls::users,      ico_users(),       i18n::nav_users,       active, i18n::nav_params),
            nav_link(urls::contact,    ico_contact(),     i18n::nav_contact,     active, i18n::nav_forms),
            nav_link(urls::about,      ico_about(),       i18n::nav_about,       active),
            nav_link(urls::components, ico_components(),  i18n::nav_components,  active, i18n::nav_ui),
            nav_link(urls::live,       ico_live(),        i18n::nav_live,        active, BADGE_SSE),
            nav_link(urls::interactive,ico_interactive(), i18n::nav_interactive, active, BADGE_JS),
            nav_link(urls::chat,       ico_chat(),        i18n::nav_chat,        active, "opt"),
            nav_link(urls::upload,     ico_upload(),      i18n::nav_upload,      active),
            html::div{styles::sb_section, css::make(css::mt::_2), i18n::nav_nested},
            nav_link(urls::about_team, ico_team(),        i18n::nav_about,       active, i18n::nav_nested),
        },
        html::div{styles::sb_foot,
            html::fragment("v", revet::version),
            html::code{html::attr.title("revet attribute — auto-generates form handlers"), "[[revet::form]]"},
        },
    };
}

static auto make_hdr(std::string_view title, bool dark_mode) -> html::Node {
    return html::header{
        ids::hdr,
        html::label{ids::sb_cb.for_(), styles::btn_icon, "☰"},
        html::span{styles::hdr_title, title},
        html::div{styles::hdr_right,
            i18n::locale_picker(),
            ui::theme_toggle(THEME_SCOPE, dark_mode, urls::dark_mode),
            html::span{styles::hdr_badge, i18n::hdr_badge},
        },
    };
}

[[revet::export]]
html::Node breadcrumb(std::string_view href, std::string_view label) {
    return html::a{html::attr.href(href), styles::bc_link, label} +
           html::span{styles::bc_sep, "/ "};
}

[[revet::export]]
web::Response layout_page(const web::Request& req, std::string_view title,
                           std::string_view active, html::Node content) {
    const bool sb_open   = web::toggles::sb_open(req.cookies);
    const bool dark_mode = web::toggles::dark_mode(req.cookies);

    return web::to_response(
        html::html_doc{
            make_head(title),
            html::body{
                web::csrf::htmx_headers(req),
                html::checkbox{ids::sb_cb,
                               sb_open ? html::attr.checked() : html::Node{},
                               htmx::attr::post(urls::sb_open),
                               htmx::attr::trigger("change"),
                               htmx::attr::swap("none")},
                make_sidebar(active),
                html::div{ids::wrap,
                    make_hdr(title, dark_mode),
                    html::main_el{ids::main, std::move(content)},
                },
            },
        }
    );
}
