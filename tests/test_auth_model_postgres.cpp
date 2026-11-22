// Randomised model check of login tokens on Postgres. Skipped unless REVET_TEST_PG_URL is set, for example:
//   REVET_TEST_PG_URL=postgres://postgres:pw@127.0.0.1:55432/revet_test
// Creates and drops the auth_* tables, so use a throwaway database.
#include <cstdlib>

#include <revet/auth/tokens.hpp>
#include <revet/auth/users.hpp>
#include <revet/db/postgres.hpp>

namespace {

using W = db::postgres::Handle<db::Write>;
using R = db::postgres::Handle<db::Read>;

void drop_all(W& w) {
    w.exec("DROP TABLE IF EXISTS auth_sessions");
    w.exec("DROP TABLE IF EXISTS auth_identities");
    w.exec("DROP TABLE IF EXISTS auth_users");
}

struct AuthDb {
    db::postgres::Pool pool;
    AuthDb() : pool(std::getenv("REVET_TEST_PG_URL"), 8) {
        W w(pool);
        drop_all(w);
        web::auth::create_schema(w);
        web::auth::tokens::create_schema(w);
    }
    ~AuthDb() {
        try {
            W w(pool);
            drop_all(w);
        } catch (...) { // best effort
        }
    }
    AuthDb(const AuthDb&) = delete;
    AuthDb& operator=(const AuthDb&) = delete;
};

auto count(W& h, const char* table) -> long long {
    return h.scalar<long long>(std::string("SELECT COUNT(*) FROM ") + table).value_or(-1);
}

} // namespace

#define AUTH_REQUIRE_DB()                                  \
    do {                                                   \
        if (std::getenv("REVET_TEST_PG_URL") == nullptr) { \
            SKIP("REVET_TEST_PG_URL not set");             \
        }                                                  \
    } while (false)

#include "auth_model_cases.hpp"
