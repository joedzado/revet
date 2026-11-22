#include <revet/auth/limits.hpp>
#include <revet/auth/session_http.hpp>

// GET /api/v1/auth/devices: the signed-in devices
static auto render_get(const web::Request& req) -> web::Response {
    return web::auth::session::http::devices(req, [] { return resources::users_db<db::Write>(); });
}

static auto api_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::signed_in();
}
