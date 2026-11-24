// Magic-link sign-in on SQLite, through the same Pool/Handle API apps use.
#include <atomic>
#include <cstdio>
#include <revet/auth/magic.hpp>
#include <revet/auth/passkeys.hpp>
#include <revet/auth/tokens.hpp>
#include <revet/auth/users.hpp>
#include <revet/db/sqlite.hpp>

namespace {

using W = db::sqlite::Handle<db::Write>;
using R = db::sqlite::Handle<db::Read>;

struct AuthDb {
    std::string path;
    db::sqlite::Pool pool;
    static auto unique_path() -> std::string {
        static std::atomic<int> seq{0};
        const auto p = "/tmp/revet_auth_magic_" + std::to_string(seq++) + ".db";
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::remove((p + suffix).c_str()); // left by a crashed run
        }
        return p;
    }
    AuthDb() : path(unique_path()), pool(path, 4) {
        W w(pool);
        web::auth::create_schema(w);
        web::auth::tokens::create_schema(w);
        web::auth::passkeys::create_schema(w);
        web::auth::magic::create_schema(w);
    }
    ~AuthDb() {
        std::remove(path.c_str());
        std::remove((path + "-wal").c_str());
        std::remove((path + "-shm").c_str());
    }
    AuthDb(const AuthDb&) = delete;
    AuthDb& operator=(const AuthDb&) = delete;
};

auto count(W& h, const char* table) -> long long {
    return h.scalar<long long>(std::string("SELECT COUNT(*) FROM ") + table).value_or(-1);
}

} // namespace

#define AUTH_REQUIRE_DB() ((void)0)
#include "auth_magic_cases.hpp"
