#include <revet/web.hpp>
#include "components.hpp"
#include "resources.hpp"
#include "queries.hpp"

static auto render_get(const web::Request& req) -> web::Task<web::Response> {
    const auto db         = resources::users_db();
    const auto user_count = static_cast<int>((co_await queries::users::all(db)).size());

    const auto content = html::div{styles::container, styles::page_content,
        html::h1{styles::page_title, "About revet"},
        html::p{"revet is a file-based, server-rendered web framework for C++23. "
                "Routes are generated at compile time from the filesystem. "
                "State is distributed via encrypted cookies — no session database needed."},

        html::h2{styles::page_subtitle, "Features"},
        html::ul{styles::feature_list,
            html::li{"File-based routing — drop a .cpp file in app/routes/, get a route"},
            html::li{"Parameterized routes — [id].cpp becomes /path/:id"},
            html::li{"SQLite integration via codegen from .sql files"},
            html::li{"Typed JSON models with from_row / from_json / to_json"},
            html::li{"Encrypted state cookies (XChaCha20-Poly1305)"},
            html::li{"Flash messages for post-redirect-get flows"},
            html::li{"CSRF protection built-in — token generation, form injection, middleware"},
            html::li{"HTML & CSS DSL — composable, type-safe, XSS-escaped by default"},
            html::li{"Zero-JS by default — global middleware via web::use()"}
        },

        html::div{styles::about_stat,
            html::span{"Users in this demo: "},
            badge(html::fragment(user_count, " members"))
        }
    };

    co_return layout_main(req, "About — {{name}}", content);
}
