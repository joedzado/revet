#include <revet/auth/limits.hpp>
#include <revet/auth/passkeys.hpp>
#include <revet/json.hpp>

#include "json_models.hpp"

// POST /passkey/register/finish: verify the new passkey. A new account is signed in with a cookie session;
// a signed-in user can only finish a challenge issued to them.
static auto render_post(const web::Request& req, web::State<Session> session) -> web::Response {
    namespace pk = web::auth::passkeys;
    std::string challenge_id, client_data, attestation, label;
    try {
        revet::json::Document doc(req.raw_body);
        auto root = doc.root();
        challenge_id = root.get("challenge_id", "");
        client_data = root.get("client_data_json", "");
        attestation = root.get("attestation_object", "");
        label = root.get("label", "");
    } catch (const std::exception&) {
        return web::api::error(web::status::bad_request, "bad_request");
    }
    if (label.size() > 128) {
        label.resize(128);
    }

    auto db = resources::users_db<db::Write>();
    const auto done =
        pk::finish_registration(db, challenge_id, client_data, attestation, label,
                                session ? pk::Expect::user(session->username) : pk::Expect::new_account());
    if (!done) {
        const auto code =
            done.error().kind == pk::Failure::Kind::credential_taken ? "credential_taken" : "registration_failed";
        log::warn("passkey registration refused (kind {})", static_cast<int>(done.error().kind));
        return web::api::error(done.error().kind == pk::Failure::Kind::credential_taken ? web::status::conflict
                                                                                        : web::status::bad_request,
                               code);
    }

    web::Response resp{.body = R"({"ok":true})", .content_type = "application/json"};
    if (done->new_user) {
        resp.state(Session{.username = done->user_id});
    }
    return resp;
}

static auto route_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::anonymous();
}
