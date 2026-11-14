#include <revet/auth/limits.hpp>
#include <revet/auth/passkeys_http.hpp>

// POST /api/v1/auth/passkey/register/options: start adding a passkey (a new account, or the signed-in user's)
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::passkeys::http::register_options(req, [] { return resources::users_db<db::Write>(); });
}

static auto api_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::anonymous();
}
