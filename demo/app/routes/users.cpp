#include "users_data.hpp"
#include <revet/ui.hpp>

static auto render_get(const auto& req) {
    const auto rows = html::list(USERS, [](const auto& entry) {
        const auto& [id, info] = entry;
        const auto& [name, email] = info;
        return html::tr{
            html::td{html::div{styles::u_name,
                html::div{styles::avatar, name.substr(0, 1)},
                html::div{
                    html::a{html::attr.href(urls::users__id_(id)), name},
                    ui::tooltip(
                        html::span{styles::u_tag, i18n::user_staff_badge},
                        i18n::user_staff_tooltip
                    )
                }
            }},
            html::td{email},
            html::td{html::code{id}},
        };
    });

    const auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_users_eyebrow},
            html::h1{i18n::page_users_h1},
            html::p{i18n::page_users_body},
        } +
        sec_label(i18n::section_staff_directory) +
        html::table{styles::u_table,
            html::thead{html::tr{
                html::th{i18n::col_name}, html::th{i18n::col_email}, html::th{i18n::col_id},
            }},
            html::tbody{std::move(rows)},
        };

    return layout_page(req, i18n::page_users_h1, urls::users, content);
}
