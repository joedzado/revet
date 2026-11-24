#include "resources.hpp"

#include "queries.hpp"

#include <revet/auth/magic.hpp>
#include <revet/auth/passkeys.hpp>
#include <revet/auth/tokens.hpp>
#include <revet/auth/users.hpp>
#include <revet/csrf.hpp>
#include <revet/log.hpp>
#include <revet/web.hpp>

namespace {
const log::TuTag LOG(log::tags::APP);
} // namespace

// Called by the framework before the server starts listening.
void site_init() {
    web::use([](const web::Request& req, const web::Next& next) -> web::Response {
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
    web::auth::magic::create_schema(db);

    // Passkeys belong to a domain. Set REVET_RP_ID and REVET_WEBAUTHN_ORIGINS in production
    // (and apple_app_ids / android_apps below for the native apps); "localhost" only works in dev.
    auto& passkeys = web::auth::passkeys::config();
    if (passkeys.rp.rp_id.empty()) {
        log::warn("REVET_RP_ID is not set: passkeys use \"localhost\", which only works in development");
        passkeys.rp.rp_id = "localhost";
        passkeys.rp.origins = {"http://localhost:8080"};
    }

    // Magic links point at REVET_BASE_URL (never at the request's Host header) and go out by SMTP:
    // REVET_SMTP_URL, REVET_SMTP_USER, REVET_SMTP_PASS, REVET_MAIL_FROM. In development, with no mail
    // server configured, the link is printed here instead. That is never done on a real address: a
    // link in a log is a login for anyone who can read the log.
    auto& magic = web::auth::magic::config();
    if (magic.base_url.empty()) {
        log::warn("REVET_BASE_URL is not set: magic links use http://localhost:8080, which only works in development");
        magic.base_url = "http://localhost:8080";
    }
    if (web::mail::config().smtp_url.empty()) {
        if (magic.base_url.starts_with("http://localhost")) {
            magic.send = [](web::mail::Message m) {
                log::info("[development] mail to {}: {}\n{}", m.to, m.subject, m.text);
            };
        } else {
            log::error("REVET_SMTP_URL is not set: magic-link mail cannot be sent");
        }
    }
    log::info("Database ready.");
}
