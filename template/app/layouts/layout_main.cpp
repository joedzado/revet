#include <revet/web.hpp>
#include <revet/csrf.hpp>
#include <revet/htmx.hpp>
#include "routes.hpp"
#include "queries.hpp"
#include "styles.hpp"

static constexpr html::id SB_ID{"sb-toggle"};

[[revet::export]]
web::Response layout_main(const web::Request& req, std::string_view title, html::Node inner) {
    const auto db         = resources::users_db();
    const auto count_rows = queries::users::sync_count(db);
    int  user_count = (!count_rows.empty() && !count_rows[0].empty() && !count_rows[0][0].empty())
                      ? std::stoi(count_rows[0][0]) : 0;

    bool is_users = title.find("User") != std::string_view::npos;
    bool is_about = title.find("About") != std::string_view::npos;
    bool is_home  = !is_users && !is_about;

    auto nav_link = [](bool is_active, std::string_view href, std::string_view label) {
        if (is_active)
            return html::a{styles::sidebar_link, styles::active, html::attr.href(href), label};
        return html::a{styles::sidebar_link, html::attr.href(href), label};
    };

    const auto sidebar = html::aside{styles::sidebar,
        html::div{styles::sidebar_brand,
            html::a{styles::brand_link, html::attr.href(urls::index), "{{name}}"}
        },
        html::nav{styles::sidebar_nav,
            html::span{styles::sidebar_label, "Navigate"},
            nav_link(is_home,  urls::index, "Dashboard"),
            nav_link(is_users, urls::users, "Users"),
            nav_link(is_about, urls::about, "About")
        },
        html::div{styles::sidebar_stats,
            html::span{styles::sidebar_label, "Live Data"},
            html::div{styles::sidebar_stat_box,
                html::div{styles::sidebar_stat_num, user_count},
                html::div{styles::sidebar_stat_desc, "users in database"}
            }
        },
        html::div{styles::sidebar_footer,
            html::span{styles::tech_badge, "C++23"},
            html::span{styles::tech_badge, "htmx"},
            html::span{styles::tech_badge, "SQLite"},
            html::span{styles::tech_badge, "CSS"}
        }
    };

    const auto header = html::header{styles::topbar,
        html::label{SB_ID.for_(), styles::btn_icon, "☰"},
        html::span{styles::topbar_title, title},
        html::button{styles::btn_icon, styles::theme_toggle,
            html::attr.type("button"),
            html::attr.onclick("(function(){var d=document.documentElement,t=d.dataset.theme==='dark'?'light':'dark';d.dataset.theme=t;localStorage.setItem('theme',t)})()"),
            "◑"
        }
    };

    return web::to_response(html::html_doc{
        html::head{
            html::meta{html::attr.charset("utf-8")},
            html::meta{html::attr.name("viewport"),
                       html::attr.content("width=device-width,initial-scale=1")},
            html::title{std::string(title)},
            html::script{html::raw("(function(){var t=localStorage.getItem('theme');if(t)document.documentElement.dataset.theme=t;})()")},
            html::collect_styles(),
            htmx::script_tag(),
            htmx::optimistic_script_tag(),
        },
        html::body{
            web::csrf::htmx_headers(req),
            html::checkbox{SB_ID, styles::sb_toggle_input},
            sidebar,
            html::div{styles::main_wrapper,
                header,
                html::main_el{styles::main_content, inner},
                html::footer{
                    html::span{"Built with revet — C++23 server-rendered framework"},
                    html::span{"© 2026 {{name}}"}
                }
            }
        }
    });
}
