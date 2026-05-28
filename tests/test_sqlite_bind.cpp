#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <revet/db/sqlite.hpp>
#include <string>

namespace {

std::string unique_db_path() {
    static std::atomic<int> seq{0};
    return std::string("/tmp/revet_test_") + std::to_string(seq++) + ".db";
}

struct TempDb {
    std::string path;
    db::sqlite::Pool pool;

    TempDb() : path(unique_db_path()), pool(path, 2) {
        auto h = db::sqlite::Handle<db::Write>(pool);
        h.exec("CREATE TABLE t (i INTEGER, big INTEGER, d REAL, s TEXT)");
    }
    ~TempDb() {
        (void)std::remove(path.c_str());
    }
    TempDb(const TempDb&) = delete;
    TempDb& operator=(const TempDb&) = delete;
    TempDb(TempDb&&) = delete;
    TempDb& operator=(TempDb&&) = delete;
};

} // namespace

TEST_CASE("bind_arg binds numeric types natively (not as TEXT)") {
    TempDb fx;
    auto w = db::sqlite::Handle<db::Write>(fx.pool);

    const int i = 7;
    const int64_t big = 9'000'000'000LL;
    const double d = 3.5;
    const std::string s = "hi";

    w.exec("INSERT INTO t VALUES (?, ?, ?, ?)", i, big, d, s);

    auto r = db::sqlite::Handle<>(fx.pool);
    auto type_of = r.query("SELECT typeof(i), typeof(big), typeof(d), typeof(s) FROM t");
    REQUIRE(type_of.size() == 1);
    REQUIRE(type_of[0][0] == "integer");
    REQUIRE(type_of[0][1] == "integer");
    REQUIRE(type_of[0][2] == "real");
    REQUIRE(type_of[0][3] == "text");
}

TEST_CASE("bind_arg with double param preserves numeric comparison semantics") {
    TempDb fx;
    auto w = db::sqlite::Handle<db::Write>(fx.pool);

    w.exec("INSERT INTO t VALUES (?, ?, ?, ?)", 1, 0, 2.5, std::string("a"));
    w.exec("INSERT INTO t VALUES (?, ?, ?, ?)", 2, 0, 10.5, std::string("b"));

    auto r = db::sqlite::Handle<>(fx.pool);
    auto rows = r.query("SELECT s FROM t WHERE d >= ? ORDER BY d", 5.0);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0][0] == "b");
}

TEST_CASE("bind_arg still accepts string args") {
    TempDb fx;
    auto w = db::sqlite::Handle<db::Write>(fx.pool);

    w.exec("INSERT INTO t VALUES (?, ?, ?, ?)", 1, 0, 0.0, std::string("needle"));
    w.exec("INSERT INTO t VALUES (?, ?, ?, ?)", 2, 0, 0.0, std::string("hay"));

    auto r = db::sqlite::Handle<>(fx.pool);
    auto rows = r.query("SELECT i FROM t WHERE s = ?", std::string("needle"));
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0][0] == "1");
}

// Named params: simulate generated code — struct + positional ? binding
TEST_CASE("named params: positional ? binding matches named intent") {
    TempDb fx;
    auto w = db::sqlite::Handle<db::Write>(fx.pool);

    w.exec("INSERT INTO t VALUES (?, ?, ?, ?)", 10, 0, 0.0, std::string("alice"));
    w.exec("INSERT INTO t VALUES (?, ?, ?, ?)", 20, 0, 0.0, std::string("bob"));

    // Simulates generated code: struct fields passed as positional ? args
    struct FindParams {
        int min_i;
        std::string name;
    };
    FindParams p{10, "alice"};

    auto r = db::sqlite::Handle<>(fx.pool);
    auto rows = r.query("SELECT s FROM t WHERE i >= ? AND s = ?", p.min_i, p.name);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0][0] == "alice");
}

TEST_CASE("named params: repeated param passed multiple times") {
    TempDb fx;
    auto w = db::sqlite::Handle<db::Write>(fx.pool);

    w.exec("INSERT INTO t VALUES (?, ?, ?, ?)", 5, 0, 0.0, std::string("x"));
    w.exec("INSERT INTO t VALUES (?, ?, ?, ?)", 10, 0, 0.0, std::string("x"));

    struct SearchParams {
        int v;
    };
    SearchParams p{5};

    // :v appears twice in SQL → p.v, p.v as positional args
    auto r = db::sqlite::Handle<>(fx.pool);
    auto rows = r.query("SELECT i FROM t WHERE i = ? OR i = ?", p.v, p.v);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0][0] == "5");
}
