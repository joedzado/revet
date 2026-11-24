// Users and identities on Postgres. Skipped unless REVET_TEST_PG_URL is set, for example:
//   REVET_TEST_PG_URL=postgres://postgres:pw@127.0.0.1:55432/revet_test
// Creates and drops the auth_* tables and a table named child, so use a throwaway database.
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <revet/auth/users.hpp>
#include <revet/db/postgres.hpp>

namespace {

using W = db::postgres::Handle<db::Write>;

void drop_all(W& w) {
    w.exec("DROP TABLE IF EXISTS child");
    w.exec("DROP TABLE IF EXISTS auth_identities");
    w.exec("DROP TABLE IF EXISTS auth_users");
}

struct AuthDb {
    db::postgres::Pool pool;
    AuthDb() : pool(std::getenv("REVET_TEST_PG_URL"), 4) {
        W w(pool);
        drop_all(w);
        web::auth::create_schema(w);
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

#define AUTH_REQUIRE_DB()                                                                                              \
    do {                                                                                                               \
        if (std::getenv("REVET_TEST_PG_URL") == nullptr) {                                                             \
            SKIP("REVET_TEST_PG_URL not set");                                                                         \
        }                                                                                                              \
    } while (false)

#include "auth_users_cases.hpp"
