#include <revet/auth/passkeys_http.hpp>
#include <revet/auth/tokens.hpp>

// POST /api/v1/auth/passkeys/remove — remove one passkey (never the last way to sign in)
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::passkeys::http::remove(req, [] { return resources::users_db<db::Write>(); });
}

static auto api_guards() -> std::vector<web::Middleware> {
    return { web::auth::tokens::guard() };
}
