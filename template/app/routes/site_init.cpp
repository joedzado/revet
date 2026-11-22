#include "resources.hpp"

#include "queries.hpp"

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
    log::info("Database ready.");
}
