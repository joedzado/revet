#include <revet/web.hpp>
#include <revet/htmx.hpp>
#include <chrono>
using namespace std::chrono_literals;
#include "resources.hpp"
#include "queries.hpp"
#include "json_models.hpp"

static constexpr html::id USER_RESULTS("user-results");

static auto render_get(const web::Request& req, web::Flash<Toast> flash) -> web::Task<web::Response> {
    const auto db   = resources::users_db();
    const auto rows = co_await queries::users::all(db);

    const auto toast = flash
        ? html::Node{html::div{styles::toast, flash->kind == "error" ? styles::toast_error : styles::toast_success, flash->message}}
        : html::Node{};

    const auto items = html::list(rows, [](const auto& u) -> html::Node {
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

    const auto content = html::div{styles::container, styles::page_content,
        toast,
        html::div{styles::page_header,
            html::h1{styles::page_title, "Users"},
            html::a{styles::btn, html::attr.href(urls::users_new), "+ Add User"}
        },
        html::div{styles::search_bar,
            html::search_input{
                html::attr.name("q"),
                html::attr.placeholder("Search users…"),
                htmx::attr::get(urls::users_search),
                htmx::attr::target(USER_RESULTS),
                htmx::attr::trigger_input(300ms),
                htmx::attr::swap_outer_html(),
            }
        },
        html::div{USER_RESULTS,
            html::div{styles::user_list,
                html::div{styles::user_list_header,
                    html::span{"Members"},
                    html::span{styles::count_badge, rows.size(), rows.size() == 1 ? " member" : " members"}
                },
                items
            }
        }
    };

    co_return layout_main(req, "Users — {{name}}", content);
}
