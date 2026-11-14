#include <revet/auth/passkeys_http.hpp>
#include <revet/auth/tokens.hpp>

// GET /api/v1/auth/passkeys — the signed-in user's passkeys
static auto render_get(const web::Request& req) -> web::Response {
    return web::auth::passkeys::http::list(req, [] { return resources::users_db<db::Write>(); });
}

static auto api_guards() -> std::vector<web::Middleware> {
    return { web::auth::tokens::guard() };
}
