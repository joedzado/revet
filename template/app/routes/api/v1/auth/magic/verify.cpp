#include <revet/auth/limits.hpp>
#include <revet/auth/magic_http.hpp>

// POST /api/v1/auth/magic/verify: the app posts the token from the link it received and gets a session.
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::magic::http::verify(req, [] { return resources::users_db<db::Write>(); });
}

static auto api_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::anonymous();
}
