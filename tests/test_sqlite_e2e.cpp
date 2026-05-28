// End-to-end SQLite CRUD via the same Pool/Handle/intent-typed API the
// generated queries module uses. Complements test_sqlite_bind.cpp which
// only checks bind type fidelity.
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <revet/db/sqlite.hpp>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string unique_db_path() {
    static std::atomic<int> seq{0};
    return "/tmp/revet_e2e_" + std::to_string(seq++) + ".db";
}

// Mirrors the template's users.sql schema (`gen_queries` would normally
// emit these). Keeping it inline here so the test never depends on a
// project's generated headers.
struct UserDb {
    std::string      path;
    db::sqlite::Pool pool;
    UserDb() : path(unique_db_path()), pool(path, 4) {
        auto w = db::sqlite::Handle<db::Write>(pool);
        w.exec(
            "CREATE TABLE users ("
            " id    INTEGER PRIMARY KEY AUTOINCREMENT,"
            " name  TEXT NOT NULL,"
            " email TEXT NOT NULL UNIQUE)");
    }
    ~UserDb() { std::remove(path.c_str()); }
    UserDb(const UserDb&)            = delete;
    UserDb& operator=(const UserDb&) = delete;
    UserDb(UserDb&&)                 = delete;
    UserDb& operator=(UserDb&&)      = delete;
};

} // namespace

TEST_CASE("sqlite e2e: empty table returns no rows", "[db][sqlite]") {
    UserDb fx;
    auto r = db::sqlite::Handle<>(fx.pool);
    REQUIRE(r.query("SELECT id FROM users").empty());
}

TEST_CASE("sqlite e2e: insert + select round-trip", "[db][sqlite]") {
    UserDb fx;
    {
        auto w = db::sqlite::Handle<db::Write>(fx.pool);
        w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
               std::string("Alice"), std::string("alice@example.com"));
    }
    auto rows = db::sqlite::Handle<>(fx.pool).query(
        "SELECT name, email FROM users WHERE id = ?", 1);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0][0] == "Alice");
    REQUIRE(rows[0][1] == "alice@example.com");
}

TEST_CASE("sqlite e2e: update propagates", "[db][sqlite]") {
    UserDb fx;
    {
        auto w = db::sqlite::Handle<db::Write>(fx.pool);
        w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
               std::string("Bob"), std::string("bob@example.com"));
        w.exec("UPDATE users SET name = ? WHERE email = ?",
               std::string("Robert"), std::string("bob@example.com"));
    }
    auto rows = db::sqlite::Handle<>(fx.pool).query(
        "SELECT name FROM users WHERE email = ?",
        std::string("bob@example.com"));
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0][0] == "Robert");
}

TEST_CASE("sqlite e2e: delete removes the row", "[db][sqlite]") {
    UserDb fx;
    {
        auto w = db::sqlite::Handle<db::Write>(fx.pool);
        w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
               std::string("Carol"), std::string("carol@example.com"));
        w.exec("DELETE FROM users WHERE email = ?",
               std::string("carol@example.com"));
    }
    REQUIRE(db::sqlite::Handle<>(fx.pool).query("SELECT id FROM users").empty());
}

TEST_CASE("sqlite e2e: UNIQUE constraint violation throws", "[db][sqlite]") {
    UserDb fx;
    auto w = db::sqlite::Handle<db::Write>(fx.pool);
    w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
           std::string("Dan"), std::string("dup@example.com"));
    REQUIRE_THROWS_AS(
        w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
               std::string("Dan2"), std::string("dup@example.com")),
        std::runtime_error);
    // First insert is still there — the second one failed, did not
    // silently corrupt or roll back the first.
    REQUIRE(db::sqlite::Handle<>(fx.pool)
                .query("SELECT COUNT(*) FROM users")[0][0] == "1");
}

TEST_CASE("sqlite e2e: prepare failure on bad SQL throws", "[db][sqlite]") {
    UserDb fx;
    auto h = db::sqlite::Handle<>(fx.pool);
    REQUIRE_THROWS_AS(h.query("NOT VALID SQL"), std::runtime_error);
}

TEST_CASE("sqlite e2e: LIKE search with parameter binding", "[db][sqlite]") {
    UserDb fx;
    {
        auto w = db::sqlite::Handle<db::Write>(fx.pool);
        w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
               std::string("Alice"),   std::string("alice@x"));
        w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
               std::string("Allison"), std::string("allison@x"));
        w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
               std::string("Bob"),     std::string("bob@x"));
    }
    auto rows = db::sqlite::Handle<>(fx.pool).query(
        "SELECT name FROM users "
        "WHERE name LIKE '%' || ? || '%' "
        "ORDER BY name",
        std::string("Al"));
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0][0] == "Alice");
    REQUIRE(rows[1][0] == "Allison");
}

TEST_CASE("sqlite e2e: parameter binding defeats injection", "[db][sqlite][security]") {
    // Classic injection payload supplied through a bound parameter is
    // treated as data, not SQL.
    UserDb fx;
    auto w = db::sqlite::Handle<db::Write>(fx.pool);
    w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
           std::string("Eve"), std::string("eve@example.com"));

    auto rows = db::sqlite::Handle<>(fx.pool).query(
        "SELECT name FROM users WHERE email = ?",
        std::string("eve@example.com'; DROP TABLE users; --"));
    REQUIRE(rows.empty());
    // Table still exists — bind interpreted the payload as a literal.
    REQUIRE(db::sqlite::Handle<>(fx.pool)
                .query("SELECT COUNT(*) FROM users")[0][0] == "1");
}

TEST_CASE("sqlite e2e: pool serves multiple reader handles", "[db][sqlite]") {
    UserDb fx;
    {
        auto w = db::sqlite::Handle<db::Write>(fx.pool);
        for (int i = 0; i < 50; ++i) {
            w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
                   std::string("u") + std::to_string(i),
                   std::string("u")  + std::to_string(i) + "@x");
        }
    }
    std::atomic<int> total{0};
    std::vector<std::thread> ts;
    ts.reserve(8);
    for (int i = 0; i < 8; ++i) {
        ts.emplace_back([&] {
            auto r    = db::sqlite::Handle<>(fx.pool);
            auto rows = r.query("SELECT COUNT(*) FROM users");
            total += std::stoi(rows[0][0]);
        });
    }
    for (auto& t : ts) t.join();
    REQUIRE(total == 8 * 50);
}

TEST_CASE("sqlite e2e: writes serialized through Handle<Write>", "[db][sqlite]") {
    // Concurrent writers each insert N rows. Pool's write mutex must
    // serialize them so the final count is exact.
    UserDb fx;
    constexpr int N_THREADS = 4;
    constexpr int N_INSERTS = 25;

    std::vector<std::thread> ts;
    ts.reserve(N_THREADS);
    for (int t = 0; t < N_THREADS; ++t) {
        ts.emplace_back([&, t] {
            auto w = db::sqlite::Handle<db::Write>(fx.pool);
            for (int i = 0; i < N_INSERTS; ++i) {
                w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
                       std::string("u"),
                       std::string("u_") + std::to_string(t)
                           + "_" + std::to_string(i) + "@x");
            }
        });
    }
    for (auto& th : ts) th.join();

    auto rows = db::sqlite::Handle<>(fx.pool)
                    .query("SELECT COUNT(*) FROM users");
    REQUIRE(rows[0][0] == std::to_string(N_THREADS * N_INSERTS));
}

TEST_CASE("sqlite e2e: BEGIN/ROLLBACK leaves table unchanged", "[db][sqlite]") {
    UserDb fx;
    auto w = db::sqlite::Handle<db::Write>(fx.pool);
    w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
           std::string("Pre"), std::string("pre@x"));
    w.exec("BEGIN");
    w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
           std::string("InTx"), std::string("intx@x"));
    w.exec("ROLLBACK");
    auto rows = db::sqlite::Handle<>(fx.pool)
                    .query("SELECT name FROM users ORDER BY id");
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0][0] == "Pre");
}

TEST_CASE("sqlite e2e: BEGIN/COMMIT persists", "[db][sqlite]") {
    UserDb fx;
    auto w = db::sqlite::Handle<db::Write>(fx.pool);
    w.exec("BEGIN");
    w.exec("INSERT INTO users (name, email) VALUES (?, ?)",
           std::string("Commit"), std::string("commit@x"));
    w.exec("COMMIT");
    auto rows = db::sqlite::Handle<>(fx.pool)
                    .query("SELECT name FROM users");
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0][0] == "Commit");
}
