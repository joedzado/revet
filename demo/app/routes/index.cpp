#include <revet/ui.hpp>

static auto render_get(const auto& req) {
    const auto features = card_grid(
        card("routing",    i18n::feat_routing_title, i18n::feat_routing_body) +
        card("forms",      i18n::feat_forms_title,   i18n::feat_forms_body) +
        card("htmx",       i18n::feat_htmx_title,    i18n::feat_htmx_body) +
        card("security",   i18n::feat_security_title, i18n::feat_security_body) +
        card("middleware", i18n::feat_guards_title,   i18n::feat_guards_body) +
        card("logging",    i18n::feat_logging_title,  i18n::feat_logging_body)
    );

    static constexpr auto SNIPPET_ROUTING =
"// site/users/[id].cpp  ->  GET /users/:id\n"
"#include <revet/web.hpp>\n"
"\n"
"static auto render_get(const auto& req) {\n"
"    const auto id = req.params.get(\"id\");\n"
"    return html::div{\n"
"        html::h1{\"User \" + id},\n"
"        html::p{\"Found via parameterized route.\"},\n"
"    };\n"
"}";

    static constexpr auto SNIPPET_FORMS =
"// forms/contact.hpp\n"
"struct [[revet::form]] ContactForm {\n"
"    revet::Field<revet::FullName, validate::required_t,\n"
"                                  validate::max_len_t<100>> name;\n"
"    revet::Field<revet::Email,    validate::required_t,\n"
"                                  validate::max_len_t<254>> email;\n"
"    revet::Field<revet::Textarea, validate::required_t,\n"
"                                  validate::min_len_t<10>,\n"
"                                  validate::max_len_t<1000>> message;\n"
"    std::optional<revet::Field<revet::Tel>> phone;\n"
"};\n"
"\n"
"// In the handler (populate/validate are generated):\n"
"ContactForm f;\n"
"populate(f, req.form);   // fills fields from POST body\n"
"auto result = validate(f); // sets .error on each field";

    static constexpr auto SNIPPET_GUARD =
"// site/admin/dashboard.cpp  --  per-route guard\n"
"static auto route_" "guards() -> std::vector<web::Middleware> {\n"
"    return {\n"
"        [](const web::Request& req, web::Next next) -> web::Response {\n"
"            if (req.cookies.get(\"session\").empty())\n"
"                return web::redirect(urls::login);\n"
"            return next();\n"
"        }\n"
"    };\n"
"}\n"
"\n"
"static auto render_get(const auto& req) {\n"
"    return html::div{ html::h1{\"Dashboard\"} };\n"
"}";

    static constexpr auto SNIPPET_MW =
"// main.cpp  --  global middleware + startup\n"
"web::use([](const web::Request& req, const web::Next& next) {\n"
"    log::info(\"{} {}\", req.method, req.path);\n"
"    auto res = next();\n"
"    res.headers[\"X-Powered-By\"] = \"revet/0.1\";\n"
"    return res;\n"
"});\n"
"\n"
"web::listen(all_routes);   // port via --port flag or ServerConfig";

    static constexpr auto HERO_SNIPPET =
"// site/index.cpp  ->  GET /\n"
"static auto render_get(const auto& req) {\n"
"    return html::div{\n"
"        html::h1{\"C++ web, the modern way.\"},\n"
"        html::p{\"Server-rendered. Type-safe. Zero JS.\"},\n"
"    };\n"
"}";

    const auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_home_eyebrow},
            html::h1{i18n::page_home_h1},
            html::p{i18n::page_home_body, " ", html::code{"C++23"}, ". ", i18n::page_home_body_suffix},
            html::div{styles::hero_code,
                html::div{styles::hero_code_bar,
                    html::span{styles::hero_code_dot},
                    html::span{styles::hero_code_dot},
                    html::span{styles::hero_code_dot},
                    " site/index.cpp → GET /",
                },
                html::pre{HERO_SNIPPET},
            },
            html::div{styles::hero_btns,
                html::a{html::attr.href(urls::contact),    styles::btn, styles::btn_solid, i18n::btn_try_form},
                html::a{html::attr.href(urls::users),      styles::btn, styles::btn_ghost, i18n::btn_browse_users},
                html::a{html::attr.href(urls::components), styles::btn, styles::btn_ghost, i18n::btn_ui_components},
            },
        } +
        html::div{styles::stat_strip,
            html::div{styles::stat,
                html::span{styles::stat_n, "0"},
                html::span{styles::stat_l, i18n::stat_lines_js},
            },
            html::div{styles::stat,
                html::span{styles::stat_n, "~2k"},
                html::span{styles::stat_l, i18n::stat_lines_core},
            },
            html::div{styles::stat,
                html::span{styles::stat_n, "C++23"},
                html::span{styles::stat_l, i18n::stat_standard},
            },
            html::div{styles::stat,
                html::span{styles::stat_n, "∞"},
                html::span{styles::stat_l, i18n::stat_checks},
            },
        } +
        sec_label(i18n::section_features) +
        features +
        html::h2{i18n::section_quick_look} +
        html::p{
            i18n::section_quick_look_body, " ",
            html::code{"html::"}, " types that compose into valid HTML at compile time.",
        } +
        ui::tabs("ql", {
            {"routing",    i18n::tab_routing,
             html::fragment(html::p{styles::tab_intro, i18n::tab_routing_intro},
                            html::pre{SNIPPET_ROUTING})},
            {"forms",      i18n::tab_forms,
             html::fragment(html::p{styles::tab_intro, i18n::tab_forms_intro},
                            html::pre{SNIPPET_FORMS})},
            {"guards",     i18n::tab_guards,
             html::fragment(html::p{styles::tab_intro, i18n::tab_guards_intro},
                            html::pre{SNIPPET_GUARD})},
            {"middleware", i18n::tab_middleware,
             html::fragment(html::p{styles::tab_intro, i18n::tab_middleware_intro},
                            html::pre{SNIPPET_MW})},
        });

    return layout_page(req, i18n::page_home_h1, urls::index, content);
}
