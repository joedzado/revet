#include <revet/auth/limits.hpp>
#include <revet/auth/session_http.hpp>

// POST /api/v1/auth/account/delete: delete the account and everything that belongs to it
static auto render_post(const web::Request& req) -> web::Response {
    return web::auth::session::http::delete_account(req, [] { return resources::users_db<db::Write>(); });
}

static auto api_guards() -> std::vector<web::Middleware> {
    return web::auth::limits::signed_in();
}
