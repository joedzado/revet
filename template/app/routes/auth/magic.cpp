#include <revet/auth/magic.hpp>
#include <revet/csrf.hpp>

#include "json_models.hpp"

// GET /auth/magic?token=...: where the link in the email lands. It only looks at the token; the button
// below it is what signs in. That way a mail scanner that opens every link in a message cannot use one up.
static auto render_get(const web::Request& req) -> web::Response {
    namespace magic = web::auth::magic;
    const auto token = req.query.get("token");
    std::optional<magic::Peeked> found;
    {
        auto db = resources::users_db();
        found = magic::peek(db, token);
    }

    html::Node body;
    if (!found) {
        body = html::div{
            styles::form_card, html::h1{styles::page_title, "This link has expired"},
            html::p{"Links work once and only for a short while. Ask for a new one."},
            html::div{styles::form_actions, html::a{styles::btn, html::attr.href(urls::login), "Back to sign in"}}};
    } else {
        const bool adding = found->purpose == magic::Purpose::link;
        body = html::div{
            styles::form_card, html::h1{styles::page_title, adding ? "Add this email address?" : "Sign in"},
            html::p{adding ? "Add " : "Sign in as ", html::strong{found->email}, adding ? " to your account." : "."},
            html::form{
                html::attr.post(), html::attr.action(urls::auth_magic_confirm), web::csrf::field(req),
                html::input{html::attr.type("hidden"), html::attr.name("token"), html::attr.value(token)},
                html::div{styles::form_actions, html::submit{styles::btn, adding ? "Add email address" : "Sign in"}}}};
    }
    auto resp = layout_main(req, "Sign in — {{name}}", html::div{styles::container, styles::page_content, body});
    // The token is in the address bar: keep it out of Referer headers and caches.
    resp.headers.data["Referrer-Policy"] = "no-referrer";
    resp.headers.data["Cache-Control"] = "no-store";
    return resp;
}
