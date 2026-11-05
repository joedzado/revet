#define REVET_DB_SQLITE
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <revet/web.hpp>
#include <revet/db/common.hpp>
#include <revet/db/sqlite.hpp>

namespace {
auto temp_db() -> std::string {
    const auto path = std::filesystem::temp_directory_path() / "revet_sqlite_driver_test.db";
    for (const char* suffix : {"", "-wal", "-shm"}) {
        std::filesystem::remove(path.string() + suffix);
    }
    return path.string();
}
} // namespace

TEST_CASE("sqlite: NULL is distinct from empty", "[sqlite]") {
    db::sqlite::Pool pool(temp_db(), 2);
    db::sqlite::Handle<db::Write> h(pool);
    h.exec("create table t (a text, b text)");
    h.exec("insert into t values (?, ?)", std::optional<std::string>{}, std::string{});
    const auto rows = h.query_nullable("select a, b from t");
    REQUIRE(rows.size() == 1);
    CHECK_FALSE(rows[0][0].has_value());
    REQUIRE(rows[0][1].has_value());
    CHECK(rows[0][1]->empty());
}

TEST_CASE("sqlite: optional and bool bind", "[sqlite]") {
    db::sqlite::Pool pool(temp_db(), 2);
    db::sqlite::Handle<db::Write> h(pool);
    h.exec("create table t (a text, b integer)");
    h.exec("insert into t values (?, ?)", std::optional<std::string>{"x"}, true);
    CHECK(h.query("select a, b from t")[0] == db::sqlite::Row{"x", "1"});
}

TEST_CASE("sqlite: transactions commit or roll back", "[sqlite]") {
    db::sqlite::Pool pool(temp_db(), 2);
    db::sqlite::Handle<db::Write> h(pool);
    h.exec("create table t (a integer)");
    {
        db::sqlite::Transaction tx(h);
        h.exec("insert into t values (1)");
    }
    CHECK(h.query("select count(*) from t")[0][0] == "0");
    {
        db::sqlite::Transaction tx(h);
        h.exec("insert into t values (1)");
        tx.commit();
    }
    CHECK(h.query("select count(*) from t")[0][0] == "1");
}

TEST_CASE("sqlite: connections use WAL and enforce foreign keys", "[sqlite]") {
    db::sqlite::Pool pool(temp_db(), 2);
    db::sqlite::Handle<db::Read> h(pool);
    CHECK(h.query("pragma journal_mode")[0][0] == "wal");
    CHECK(h.query("pragma foreign_keys")[0][0] == "1");
}

TEST_CASE("sqlite: SQL need not be NUL-terminated", "[sqlite]") {
    db::sqlite::Pool pool(temp_db(), 2);
    db::sqlite::Handle<db::Read> h(pool);
    const std::string text = "select 1 garbage that must not be read";
    CHECK(h.query(std::string_view(text).substr(0, 8))[0][0] == "1");
    CHECK(h.query(std::string_view(text).substr(0, 8), std::nullopt) .size() == 1);
}
