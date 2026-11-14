#include <revet/auth/passkeys_http.hpp>
#include <revet/auth/tokens.hpp>

// POST /api/v1/auth/passkey/login/options — start a sign-in; no username needed
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::passkeys::http::login_options(req, [] { return resources::users_db<db::Write>(); });
}

