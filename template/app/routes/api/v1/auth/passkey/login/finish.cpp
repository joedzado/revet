#include <revet/auth/passkeys_http.hpp>
#include <revet/auth/tokens.hpp>

// POST /api/v1/auth/passkey/login/finish — verify the sign-in and return a session
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::passkeys::http::login_finish(req, [] { return resources::users_db<db::Write>(); });
}

