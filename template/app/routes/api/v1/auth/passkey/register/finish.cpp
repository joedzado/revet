#include <revet/auth/passkeys_http.hpp>
#include <revet/auth/tokens.hpp>

// POST /api/v1/auth/passkey/register/finish — verify the new passkey; a new account comes back signed in
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::passkeys::http::register_finish(req, [] { return resources::users_db<db::Write>(); });
}

