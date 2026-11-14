#include <revet/auth/limits.hpp>
#include <revet/auth/passkeys.hpp>
#include <revet/json.hpp>
#include "json_models.hpp"

// POST /passkey/login/finish: verify the passkey and start a cookie session.
// The Session cookie holds the user id (Session::username).
static auto render_post(const web::Request& req) -> web::Response {
    namespace pk = web::auth::passkeys;
    std::string challenge_id, credential_id, client_data, auth_data, signature, handle;
    try {
        revet::json::Document doc(req.raw_body);
        auto root = doc.root();
        challenge_id  = root.get("challenge_id", "");
        credential_id = root.get("credential_id", "");
        client_data   = root.get("client_data_json", "");
        auth_data     = root.get("authenticator_data", "");
        signature     = root.get("signature", "");
        handle        = root.get("user_handle", "");
    } catch (const std::exception&) {
        return web::api::error(web::status::bad_request, "bad_request");
    }

    auto db = resources::users_db<db::Write>();
    const auto in = pk::finish_login(db, challenge_id, credential_id, client_data, auth_data, signature, handle);
    if (!in) {
        log::warn("passkey sign-in refused (kind {})", static_cast<int>(in.error().kind));
        return web::api::error(web::status::unauthorized, "login_failed");
    }

    web::Response resp{.body = R"({"ok":true})", .content_type = "application/json"};
    resp.state(Session{.username = in->user_id});
    return resp;
}

static auto route_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::anonymous();
}
