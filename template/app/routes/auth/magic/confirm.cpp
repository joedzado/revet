#include <revet/auth/limits.hpp>
#include <revet/auth/magic.hpp>
#include <revet/csrf.hpp>

#include "json_models.hpp"

// POST /auth/magic/confirm: the button on the link page. Spends the token, then signs in (a cookie
// session holding the user id) or adds the address to the signed-in account.
static auto route_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::anonymous();
}

static auto render_post(const web::Request& req) -> web::Response {
    namespace magic = web::auth::magic;
    std::expected<magic::Completed, magic::CompleteError> done = std::unexpected(magic::CompleteError::invalid_link);
    {
        auto db = resources::users_db<db::Write>();
        done = magic::complete(db, req.form.get("token"));
    }
    if (!done) {
        const bool taken = done.error() == magic::CompleteError::email_taken;
        auto resp = layout_main(
            req, "Sign in — {{name}}",
            html::div{styles::container, styles::page_content,
                      html::div{styles::form_card,
                                html::h1{styles::page_title,
                                         taken ? "That address belongs to another account" : "This link has expired"},
                                html::p{taken ? "It is already used to sign in to a different account."
                                              : "Links work once and only for a short while. Ask for a new one."},
                                html::div{styles::form_actions,
                                          html::a{styles::btn, html::attr.href(urls::login), "Back to sign in"}}}});
        resp.status = taken ? web::status::conflict : web::status::bad_request;
        resp.headers.data["Cache-Control"] = "no-store";
        return resp;
    }
    if (done->linked) {
        return web::redirect(urls::account_passkeys);
    }
    return web::redirect(urls::index).state(Session{.username = done->user_id});
}
