#include "users_data.hpp"

static auto render_get(const auto& req) {
    const auto id = req.params.get("id");

    if (!USERS.contains(id)) {
        const auto content =
            breadcrumb(urls::users, i18n::nav_users) +
            html::div{styles::hero,
                html::h1{i18n::page_user_not_found},
                html::p{i18n::user_not_found_body},
            } +
            html::a{html::attr.href(urls::users), styles::btn, styles::btn_ghost, i18n::btn_back_to_list};

        auto r = layout_page(req, i18n::page_user_not_found, urls::users, content);
        r.status = web::status::not_found;
        return r;
    }

    const auto& [name, email] = USERS.at(id);

    const auto content =
        breadcrumb(urls::users, i18n::nav_users) +
        html::div{styles::profile,
            html::div{styles::avatar_lg, name.substr(0, 1)},
            html::div{
                html::div{styles::profile_name, name},
                html::div{styles::profile_meta,
                    i18n::label_email, " ", html::code{email}, html::br{},
                    i18n::label_id,    " ", html::span{styles::chip, id}
                }
            }
        } +
        sec_label(i18n::section_account_activity) +
        html::div{styles::cards,
            card(i18n::card_last_login, i18n::card_last_login_val, i18n::card_last_login_detail),
            card(i18n::card_status,     i18n::card_status_val,     i18n::card_status_detail),
        };

    return layout_page(req, name, urls::users, content);
}
