#include <revet/auth/passkeys.hpp>
#include <revet/auth/tokens.hpp>
#include <revet/auth/users.hpp>
#include <revet/log.hpp>
#include <revet/web.hpp>
#include <revet/csrf.hpp>
#include "resources.hpp"
#include "queries.hpp"

static const log::TuTag LOG(log::tags::APP);

void site_init() {
    web::use([](const web::Request& req, const web::Next& next) {
        log::info("{} {}", web::to_string(req.method), static_cast<std::string>(req.path));
        auto resp = next();
        log::info("  → {}", resp.status);
        return resp;
    });

    web::use(web::csrf::middleware);

    log::info("Initializing database...");
    auto db = resources::users_db<db::Write>();
    queries::users::sync_create_schema(db);
    queries::users::sync_seed(db);
    web::auth::create_schema(db);
    web::auth::tokens::create_schema(db);
    web::auth::passkeys::create_schema(db);

    // Passkeys belong to a domain. Set REVET_RP_ID and REVET_WEBAUTHN_ORIGINS in production
    // (and apple_app_ids / android_apps below for the native apps); "localhost" only works in dev.
    auto& passkeys = web::auth::passkeys::config();
    if (passkeys.rp.rp_id.empty()) {
        log::warn("REVET_RP_ID is not set: passkeys use \"localhost\", which only works in development");
        passkeys.rp.rp_id = "localhost";
        passkeys.rp.origins = {"http://localhost:8080"};
    }
    log::info("Database ready.");
}
