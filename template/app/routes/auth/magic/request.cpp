#include <revet/auth/limits.hpp>
#include <revet/auth/magic.hpp>
#include "json_models.hpp"

// POST /auth/magic/request: "email me a link". Answers the same for every valid address, so it does not
// reveal who has an account. Signed in, the link adds the address to the account instead.
static auto route_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::magic_request();
}

static auto render_post(const web::Request& req, web::State<Session> session) -> web::Response {
    namespace magic = web::auth::magic;
    magic::RequestResult result;
    {
        auto db = resources::users_db<db::Write>();
        result = magic::request(db, req.form.get("email"), session ? magic::Purpose::link : magic::Purpose::sign_in,
                                session ? std::string_view(session->username) : std::string_view());
    }
    html::Node msg;
    if (result == magic::RequestResult::invalid_email) {
        msg = html::p{styles::passkey_status, html::attr("role", "alert"), "That does not look like an email address."};
    } else if (result == magic::RequestResult::no_such_user) {
        msg = html::p{styles::passkey_status, html::attr("role", "alert"), "Sign in again, then add the address."};
    } else {
        msg = html::p{styles::passkey_status, html::attr("role", "status"),
                      "If that address can receive mail, a link is on its way. It works once, for 15 minutes."};
    }
    return web::to_response(html::div{html::attr.id("magic-status"), msg});
}
