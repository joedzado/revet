#include <revet/auth/limits.hpp>
#include <revet/auth/session_http.hpp>

// POST /api/v1/auth/refresh: trade a refresh token for a new token pair
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::session::http::refresh(req, [] { return resources::users_db<db::Write>(); });
}

static auto api_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::anonymous();
}
