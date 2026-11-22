#include <revet/auth/limits.hpp>
#include <revet/auth/passkeys_http.hpp>

// POST /passkey/login/options: the browser's copy of the JSON endpoint. CSRF is checked globally.
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::passkeys::http::login_options(req, [] { return resources::users_db<db::Write>(); });
}

static auto route_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::anonymous();
}
