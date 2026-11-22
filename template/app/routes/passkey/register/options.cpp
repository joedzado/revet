#include <revet/auth/limits.hpp>
#include <revet/auth/passkeys.hpp>
#include <revet/json.hpp>
#include <revet/json_write.hpp>
#include "json_models.hpp"

// POST /passkey/register/options: a new account, or another passkey for the signed-in user.
static auto render_post(const web::Request& req, web::State<Session> session) -> web::Response {
    namespace pk = web::auth::passkeys;
    std::string name = "user";
    try {
        if (!req.raw_body.empty()) {
            revet::json::Document doc(req.raw_body);
            if (auto n = doc.root().get("display_name", ""); !n.empty() && n.size() <= 128) name = n;
        }
    } catch (const std::exception&) {
        return web::api::error(web::status::bad_request, "bad_request");
    }

    auto db = resources::users_db<db::Write>();
    const auto opts = pk::begin_registration(db, session ? std::optional<std::string>(session->username) : std::nullopt, name);
    if (!opts) return web::api::error(web::status::unauthorized, "unauthorized");

    revet::json::Writer w;
    w.object().key("challenge_id").value(opts->challenge_id).key("options").raw(opts->options_json).end();
    return web::Response{.body = std::move(w).str(), .content_type = "application/json"};
}

static auto route_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::anonymous();
}
