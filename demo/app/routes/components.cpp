#include "routes.hpp"
#include "styles.hpp"
#include <revet/ui.hpp>

static auto render_get(const auto& req) {
    auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_components_eyebrow},
            html::h1{i18n::page_components_h1},
            html::p{i18n::page_components_body},
        } +

        sec_label(i18n::section_toggle) +
        html::p{i18n::toggle_desc} +
        ui::toggle("intro", i18n::toggle_what_label, html::p{i18n::toggle_what_body}) +
        ui::toggle("features", i18n::toggle_why_label, html::p{i18n::toggle_why_body}) +

        sec_label(i18n::section_accordion) +
        html::p{i18n::section_accordion_body} +
        ui::accordion({
            {i18n::acc_q1, html::p{i18n::acc_a1}},
            {i18n::acc_q2, html::p{i18n::acc_a2}},
            {i18n::acc_q3, html::p{i18n::acc_a3}},
        }) +

        sec_label(i18n::section_modal) +
        html::p{i18n::modal_desc} +
        ui::modal("demo-modal",
            html::span{i18n::modal_open},
            html::fragment(
                html::h3{i18n::modal_title},
                html::p{i18n::modal_body})) +

        sec_label(i18n::section_dropdown) +
        html::p{i18n::dropdown_desc} +
        ui::dropdown(
            html::Node{i18n::dropdown_options},
            ui::dropdown_item(i18n::dd_item_docs,   "/") +
            ui::dropdown_item(i18n::dd_item_source, "/about") +
            ui::dropdown_item(i18n::nav_contact,    urls::contact)) +

        sec_label(i18n::section_tabs) +
        html::p{i18n::tabs_desc} +
        ui::tabs("demo", {
            {"setup",  i18n::tab_setup,
             html::fragment(
                 html::p{i18n::tab_setup_intro},
                 html::pre{"cmake -B build && cmake --build build\n./build/myapp"})},
            {"routes", i18n::tab_routing,
             html::fragment(
                 html::p{i18n::tab_comp_routing_intro},
                 html::pre{"// site/hello.cpp  ->  GET /hello\nstatic auto render_get(const auto& req) {\n    return html::h1{\"Hello, world!\"};\n}"})},
            {"forms",  i18n::tab_forms,
             html::fragment(
                 html::p{i18n::tab_comp_forms_intro},
                 html::pre{"struct SignupForm {\n    [[revet::field(required, email)]]\n    forms::Text email;\n};"})},
        }) +

        sec_label(i18n::section_tooltip) +
        html::p{i18n::tooltip_desc} +
        html::div{styles::demo_row,
            ui::tooltip(html::span{styles::demo_chip,        i18n::tooltip_hover}, i18n::tooltip_text),
            ui::tooltip(html::span{styles::demo_chip_accent, i18n::tooltip_also},  i18n::tooltip_also_text),
        } +

        sec_label(i18n::section_carousel) +
        html::p{html::code{"scroll-snap-type: x mandatory"}, " ", i18n::section_carousel_body} +
        ui::carousel(html::list(std::array{
            std::pair{i18n::car_routing_title, i18n::car_routing_desc},
            std::pair{i18n::car_html_title,    i18n::car_html_desc},
            std::pair{i18n::car_forms_title,   i18n::car_forms_desc},
            std::pair{i18n::car_js_title,      i18n::car_js_desc},
            std::pair{i18n::car_db_title,      i18n::car_db_desc},
        }, [n = 0](const auto& card) mutable {
            return ui::carousel_item(html::div{styles::car_card,
                html::div{styles::car_num, std::format("{:02}", ++n)},
                html::h3{card.first}, html::p{styles::car_desc, card.second}});
        })) +

        sec_label(i18n::section_scroll_reveal) +
        html::p{html::code{"ui::scroll_pin"}, " ", i18n::section_scroll_body, " ",
            html::code{"view-timeline"}, " ", i18n::section_scroll_body2} +
        html::list(std::array{
            std::pair{i18n::car_routing_title, i18n::pin_routing_desc},
            std::pair{i18n::car_html_title,    i18n::pin_html_desc},
            std::pair{i18n::pin_css_title,     i18n::pin_css_desc},
        }, [n = 0](const auto& pin) mutable {
            return ui::scroll_pin(html::fragment(
                html::div{styles::pin_num, std::format("{:02}", ++n)},
                html::h3{styles::pin_h3, pin.first},
                html::p{styles::pin_desc, pin.second}));
        }) +

        sec_label(i18n::section_sidebar_theme) +
        html::p{i18n::sidebar_theme_desc} +
        html::div{styles::sb_demo_wrap, html::attr.id("sb-theme-wrap"),
            ui::sidebar("demo",
                html::fragment(
                    html::div{styles::sb_demo_hd, i18n::sb_demo_nav},
                    html::div{styles::sb_demo_link,        i18n::nav_home},
                    html::div{styles::sb_demo_link_muted,  i18n::nav_users},
                    html::div{styles::sb_demo_link_active, i18n::nav_components},
                    html::div{styles::sb_demo_row,
                        ui::scoped_theme_toggle("sb-demo", false, "#sb-theme-wrap"),
                        html::span{styles::sb_demo_hint, i18n::demo_theme_label},
                    }),
                html::p{styles::sb_demo_body, i18n::sb_demo_main})
        };

    return layout_page(req, i18n::page_components_h1, urls::components, content);
}
