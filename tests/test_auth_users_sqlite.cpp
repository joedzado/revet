// Users and identities on SQLite, through the same Pool/Handle API apps use.
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <revet/auth/users.hpp>
#include <revet/db/sqlite.hpp>

namespace {

struct AuthDb {
    std::string path;
    db::sqlite::Pool pool;
    static auto unique_path() -> std::string {
        static std::atomic<int> seq{0};
        const auto p = "/tmp/revet_auth_users_" + std::to_string(seq++) + ".db";
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::remove((p + suffix).c_str()); // left by a crashed run
        }
        return p;
    }
    AuthDb() : path(unique_path()), pool(path, 4) {
        db::sqlite::Handle<db::Write> w(pool);
        web::auth::create_schema(w);
    }
    ~AuthDb() {
        std::remove(path.c_str());
        std::remove((path + "-wal").c_str());
        std::remove((path + "-shm").c_str());
    }
    AuthDb(const AuthDb&) = delete;
    AuthDb& operator=(const AuthDb&) = delete;
};

using W = db::sqlite::Handle<db::Write>;

auto count(W& h, const char* table) -> long long {
    return h.scalar<long long>(std::string("SELECT COUNT(*) FROM ") + table).value_or(-1);
}

} // namespace

#define AUTH_REQUIRE_DB() ((void)0)
#include "auth_users_cases.hpp"

TEST_CASE("auth users: a failed create leaves no half-made user", "[auth][sqlite]") {
    AuthDb fx;
    W w(fx.pool);
    w.exec("CREATE TRIGGER refuse BEFORE INSERT ON auth_identities BEGIN SELECT RAISE(ABORT, 'refused'); END");
    REQUIRE_THROWS(auth::find_or_create_user(w, "google", "alice"));
    REQUIRE(count(w, "auth_users") == 0);
}
