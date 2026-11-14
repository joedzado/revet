#include <revet/auth/passkeys.hpp>
#include <revet/csrf.hpp>
#include "json_models.hpp"

// GET /account/passkeys: the signed-in user's passkeys, with add and remove.
static auto route_guards() -> std::vector<web::Middleware> {
    return {
        [](const web::Request& req, web::Next next) -> web::Response {
            if (!web::states::has<Session>(req.cookies))
                return web::redirect(urls::login);
            return next();
        }
    };
}

static auto render_get(const web::Request& req, web::State<Session> session) -> web::Response {
    const auto content = html::div{styles::container, styles::page_content,
        html::attr("data-csrf", web::csrf::token_from(req)),
        html::h1{styles::page_title, "Passkeys"},
        html::p{"Your passkeys sign you in without a password. Keep at least one."},
        passkey_list(session->username),
        html::div{styles::form_actions,
            html::button{styles::btn, html::attr.type("button"), html::attr("data-passkey", "add"), "Add a passkey"}
        },
        html::p{html::attr.id("passkey-status"), styles::passkey_status, html::attr("aria-live", "polite")},
        scripts::passkey_tag()
    };
    return layout_main(req, "Passkeys — {{name}}", content);
}
