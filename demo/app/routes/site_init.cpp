#include <revet/web.hpp>
#include <revet/csrf.hpp>
#include <revet/log.hpp>
#include "i18n.hpp"
#include "resources.hpp"

static const log::TuTag LOG(log::tags::APP);

void site_init() {
    web::set_public_root(paths::public_dir);
    // CSRF for HTML routes; API routes (/api/*) use Bearer token auth instead.
    web::use([](const web::Request& req, const web::Next& next) -> web::Response {
        if (std::string_view(req.path.path).starts_with("/api/"))
            return next();
        return web::csrf::middleware(req, next);
    });

    // Set thread-local locale from cookie before every request.
    web::use([](const web::Request& req, const web::Next& next) {
        const auto lang = req.cookies.get("locale");
        i18n::set_locale(lang.empty() ? "en" : lang);
        return next();
    });

    web::on_error(web::status::not_found, [](const web::Request&) {
        return web::to_response(
            html::h1{i18n::page_not_found_h1} +
            html::p{i18n::page_not_found_body}
        );
    });

    log::info("Site initialized with CSRF protection and i18n middleware.");
}
