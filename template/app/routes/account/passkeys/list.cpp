#include "json_models.hpp"

// GET /account/passkeys/list: just the list, for htmx to swap in after a passkey is added.
static auto route_guards() -> std::vector<web::Middleware> {
    return {[](const web::Request& req, web::Next next) -> web::Response {
        if (!web::states::has<Session>(req.cookies)) {
            return web::api::error(web::status::unauthorized, "unauthorized");
        }
        return next();
    }};
}

static auto render_get(const web::Request&, web::State<Session> session) -> web::Response {
    return web::to_response(passkey_list(session->username));
}
