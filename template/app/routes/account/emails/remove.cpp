#include <revet/auth/users.hpp>
#include "json_models.hpp"

// POST /account/emails/remove: take an email address off the account and return the refreshed list.
// The last way to sign in is kept.
static auto route_guards() -> std::vector<web::Middleware> {
    return {
        [](const web::Request& req, web::Next next) -> web::Response {
            if (!web::states::has<Session>(req.cookies))
                return web::api::error(web::status::unauthorized, "unauthorized");
            return next();
        }
    };
}

static auto render_post(const web::Request& req, web::State<Session> session) -> web::Response {
    web::auth::UnlinkResult result;
    {
        auto db = resources::users_db<db::Write>();
        result = web::auth::unlink_identity(db, session->username, "email", req.form.get("email"));
    }
    auto list = web::to_response(email_list(session->username));
    if (result == web::auth::UnlinkResult::last_identity) {
        list.headers.data["HX-Trigger"] = R"({"passkey-note":"That is your only way to sign in. Add a passkey or another address first."})";
    }
    return list;
}
