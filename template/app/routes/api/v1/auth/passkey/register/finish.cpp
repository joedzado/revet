#include <revet/auth/limits.hpp>
#include <revet/auth/passkeys_http.hpp>

// POST /api/v1/auth/passkey/register/finish: verify the new passkey; a new account comes back signed in
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::passkeys::http::register_finish(req, [] { return resources::users_db<db::Write>(); });
}

static auto api_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::anonymous();
}
