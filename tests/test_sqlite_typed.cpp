// Typed results (query_as, scalar), Decimal binding and backups on SQLite.
#include <catch2/catch_test_macros.hpp>
#include <revet/db/sqlite.hpp>
#include <revet/decimal.hpp>

#include <atomic>
#include <fstream>
#include <unistd.h>
#include <filesystem>
#include <string>

namespace {
std::string unique_db_path() {
    static std::atomic<int> seq{0};
    return std::format("/tmp/revet_typed_{}_{}.db", ::getpid(), seq++);
}
} // namespace

TEST_CASE("query_as and scalar return typed values", "[sqlite]") {
    const auto path = unique_db_path();
    db::sqlite::Pool pool(path, 2);
    db::sqlite::Handle<db::Write> h(pool);
    h.exec("create table t (name text not null, amount real, n integer)");
    h.exec("insert into t values (?, ?, ?)", "a", 1.5, 2);
    h.exec("insert into t values (?, ?, ?)", "b", std::nullopt, 3);

    auto rows = h.query_as<std::string, std::optional<double>, int>("select name, amount, n from t order by name");
    REQUIRE(rows.size() == 2);
    CHECK(std::get<0>(rows[0]) == "a");
    CHECK(std::get<1>(rows[0]) == 1.5);
    CHECK_FALSE(std::get<1>(rows[1]));
    CHECK(std::get<2>(rows[1]) == 3);

    CHECK(h.scalar<int64_t>("select count(*) from t") == 2);
    CHECK(h.scalar<int64_t>("select n from t where name = ?", "zzz") == std::nullopt);
    CHECK(h.scalar<double>("select amount from t where name = ?", "b") == std::nullopt);
    CHECK(h.scalar<int64_t>("with x(v) as (select sum(n) from t) select v from x") == 5);
    std::filesystem::remove(path);
}

TEST_CASE("Decimal binds as text", "[sqlite]") {
    const auto path = unique_db_path();
    db::sqlite::Pool pool(path, 1);
    db::sqlite::Handle<db::Write> h(pool);
    h.exec("create table m (v text)");
    h.exec("insert into m values (?)", revet::Decimal::parse("12.50"));
    h.exec("insert into m values (?)", std::optional<revet::Decimal>{});
    CHECK(h.scalar<std::string>("select v from m where v is not null") == "12.50");
    CHECK(h.scalar<revet::Decimal>("select v from m where v is not null")->to_string() == "12.50");
    std::filesystem::remove(path);
}

TEST_CASE("backup copies, skips a second daily copy, and prunes", "[sqlite]") {
    namespace fs = std::filesystem;
    const auto path = unique_db_path();
    const fs::path dir = path + ".backups";
    db::sqlite::Pool pool(path, 1);
    db::sqlite::Handle<db::Write> h(pool);
    h.exec("create table x (v integer)");
    h.exec("insert into x values (1)");

    fs::create_directories(dir);
    for (const char* old : {"app-2020-01-01.db", "app-2020-01-02.db", "other-2020-01-01.db"}) {
        std::ofstream(dir / old) << "old";
    }
    auto made = db::sqlite::backup(h, {.dir = dir, .prefix = "app", .keep = 2});
    REQUIRE(made);
    CHECK(fs::exists(*made));
    CHECK_FALSE(db::sqlite::backup(h, {.dir = dir, .prefix = "app", .keep = 2}));
    CHECK_FALSE(fs::exists(dir / "app-2020-01-01.db"));
    CHECK(fs::exists(dir / "app-2020-01-02.db"));
    CHECK(fs::exists(dir / "other-2020-01-01.db"));

    db::sqlite::Pool copy(made->string(), 1);
    db::sqlite::Handle<db::Read> r(copy);
    CHECK(r.query("select v from x") == db::sqlite::Result{{"1"}});
    fs::remove_all(dir);
    fs::remove(path);
}
