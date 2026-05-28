#include <revet/web.hpp>
#include <revet/htmx.hpp>
#include "resources.hpp"
#include "queries.hpp"

static constexpr html::id USER_RESULTS("user-results");

static auto render_get(const web::Request& req) -> web::Task<web::Response> {
    const auto q   = req.query.get("q");
    const auto db  = resources::users_db();
    const auto rows = co_await (q.empty() ? queries::users::all(db)
                                           : queries::users::search(db, {.q = q}));

    const auto items = rows.empty()
        ? html::Node{html::div{styles::empty_state,
            html::p{"No users match "" + q + ""."}}}
        : html::list(rows, [](const auto& u) -> html::Node {
            std::string initial = u.name.empty() ? "?" : std::string{u.name[0]};
            return html::a{styles::user_row, html::attr.href(urls::users__id_(u.id)),
                html::div{styles::user_avatar, initial},
                html::div{styles::user_info,
                    html::div{styles::user_name,  u.name},
                    html::div{styles::user_email, u.email}
                },
                html::span{styles::user_id,        "#" + u.id},
                html::span{styles::user_row_arrow, "→"}
            };
        });

    const auto list = html::div{USER_RESULTS,
        html::div{styles::user_list,
            html::div{styles::user_list_header,
                html::span{"Members"},
                html::span{styles::count_badge, rows.size(), rows.size() == 1 ? " member" : " members"}
            },
            items
        }
    };

    // HTMX request — return just the list fragment for swapping
    if (htmx::is_request(req))
        co_return web::to_response(list);

    // Direct navigation — return the full page
    const auto content = html::div{styles::container, styles::page_content, list};
    co_return layout_main(req, "Search — {{name}}", content);
}
