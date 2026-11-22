#include <revet/auth/limits.hpp>
#include <revet/auth/magic_http.hpp>

// POST /api/v1/auth/magic/request: mail a sign-in link. With an access token the link adds the address to that account.
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::magic::http::request(req, [] { return resources::users_db<db::Write>(); });
}

static auto api_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::magic_request();
}
