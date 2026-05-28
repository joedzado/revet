#include <revet/web.hpp>
#include <revet/htmx.hpp>
#include <revet/log.hpp>
#include <revet/csrf.hpp>
#include "resources.hpp"
#include "queries.hpp"
#include "json_models.hpp"

static const log::TuTag LOG(log::tags::ROUTES);

static auto render_get(const web::Request& req) -> web::Task<web::Response> {
    const auto id = req.params.get("id");
    const auto db       = resources::users_db();
    const auto rows     = co_await queries::users::find_by_id(db, {.id = std::stoi(id)});

    if (rows.empty()) {
        const auto content = html::div{styles::container, styles::page_content,
            html::a{styles::back_link, html::attr.href(urls::users), "← Users"},
            html::h1{styles::page_title, "Not found"},
            html::p{"No user with id ", id, "."}
        };
        auto resp = layout_main(req, "Not Found — {{name}}", content);
        resp.status = web::status::not_found;
        co_return resp;
    }

    const auto& u = rows[0];
    std::string initial = u.name.empty() ? "?" : std::string{u.name[0]};

    const auto content = html::div{styles::container, styles::page_content,
        html::a{styles::back_link, html::attr.href(urls::users), "← Users"},
        html::div{styles::profile_hero,
            html::div{styles::profile_avatar, initial},
            html::h1{styles::profile_name, u.name}
        },
        html::div{styles::detail_card,
            html::div{styles::detail_row,
                html::span{styles::detail_label, "Email"},
                html::span{styles::detail_value, u.email}
            },
            html::div{styles::detail_row,
                html::span{styles::detail_label, "ID"},
                html::span{styles::detail_value, "#" + u.id}
            }
        },
        html::div{styles::danger_zone,
            html::form{html::attr.post(), html::attr.action(urls::users__id__delete(u.id)),
                web::csrf::field(req),
                html::submit{styles::btn, styles::btn_danger,
                    htmx::attr::confirm("Delete this user?"),
                    "Delete User"
                }
            }
        }
    };

    co_return layout_main(req, u.name + " — {{name}}", content);
}
