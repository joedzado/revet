#include <revet/auth/passkeys.hpp>
#include "json_models.hpp"

// POST /account/passkeys/remove: remove one passkey and return the refreshed list.
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
    web::auth::passkeys::RemoveResult result;
    {
        auto db = resources::users_db<db::Write>();
        result = web::auth::passkeys::remove_passkey(db, session->username, req.form.get("credential_id"));
    }
    auto list = web::to_response(passkey_list(session->username));
    if (result == web::auth::passkeys::RemoveResult::last_login_method) {
        list.headers.data["HX-Trigger"] = R"({"passkey-note":"You need at least one passkey."})";
    }
    return list;
}
