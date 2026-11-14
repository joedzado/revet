#include <revet/auth/passkeys_http.hpp>

// GET /.well-known/apple-app-site-association — lets the iOS app use this site's passkeys.
// Set web::auth::passkeys::config().apple_app_ids in site_init.cpp; 404 until then.
static auto render_get(const web::Request& req) -> web::Response {
    return web::auth::passkeys::http::apple_app_site_association(req);
}
