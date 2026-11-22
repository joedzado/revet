#include <revet/auth/passkeys_http.hpp>

// GET /.well-known/assetlinks.json — lets the Android app use this site's passkeys.
// Set web::auth::passkeys::config().android_apps in site_init.cpp; 404 until then.
static auto render_get(const web::Request& req) -> web::Response {
    return web::auth::passkeys::http::asset_links(req);
}
