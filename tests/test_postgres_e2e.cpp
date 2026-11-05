// End-to-end tests of db::postgres against a real server. Skipped unless
// REVET_TEST_PG_URL is set, for example:
//   REVET_TEST_PG_URL=postgres://postgres:pw@127.0.0.1:55432/revet_test
// These tests create and drop tables named e2e_*, so use a throwaway database.
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <revet/db/postgres.hpp>
#include <revet/thread_pool.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using db::Write;
using db::postgres::Handle;
using db::postgres::Pool;
using db::postgres::Transaction;

namespace {

const char* test_url() {
    return std::getenv("REVET_TEST_PG_URL");
}

#define REQUIRE_PG()                                       \
    do {                                                   \
        if (test_url() == nullptr) {                       \
            SKIP("REVET_TEST_PG_URL not set");             \
        }                                                  \
    } while (false)

// Drives a Task<void> to completion on the I/O pool.
template<typename MakeTask>
void run_task(MakeTask make_task) {
    web::detail::io_pool() = std::make_unique<WorkerPool>(2);
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    std::move(make_task()).start([&] {
        std::lock_guard lk(m);
        done = true;
        cv.notify_one();
    });
    {
        std::unique_lock lk(m);
        cv.wait_for(lk, std::chrono::seconds(10), [&] { return done; });
    }
    web::detail::io_pool().reset();
    REQUIRE(done);
}

} // namespace

TEST_CASE("postgres: NULL and empty string stay distinct", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    Handle<> h(pool);

    const auto r = h.query_nullable("select null::text, ''::text, 'x'::text");
    REQUIRE(r.size() == 1);
    CHECK_FALSE(r[0][0].has_value());
    CHECK(r[0][1] == std::optional<std::string>(""));
    CHECK(r[0][2] == std::optional<std::string>("x"));

    // query() keeps its existing shape, where NULL reads as "".
    CHECK(h.query("select null::text")[0][0].empty());
}

TEST_CASE("postgres: NULL binds from nullptr and empty optional", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    Handle<> h(pool);

    const std::optional<std::string> none;
    const std::optional<std::string> some = "v";
    const auto r = h.query_nullable("select ?::text, ?::text, ?::text", nullptr, none, some);
    CHECK_FALSE(r[0][0].has_value());
    CHECK_FALSE(r[0][1].has_value());
    CHECK(r[0][2] == std::optional<std::string>("v"));
}

TEST_CASE("postgres: numeric survives exactly as text", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    Handle<Write> h(pool);
    h.exec("drop table if exists e2e_money");
    h.exec("create table e2e_money (amount numeric(19,4) not null)");

    // Neither of the first two values round-trips through a double.
    const std::vector<std::string> values = {"123456789012345.6789", "0.1000", "-0.0001", "99999.9900"};
    for (const auto& v : values) {
        h.exec("insert into e2e_money (amount) values (?::numeric)", v);
    }
    const auto rows = h.query("select amount from e2e_money order by ctid");
    REQUIRE(rows.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        CHECK(rows[i][0] == values[i]);
    }
    CHECK(h.query("select sum(amount) from e2e_money")[0][0] == "123456789112345.7688");
    h.exec("drop table e2e_money");
}

TEST_CASE("postgres: uuid, date, timestamp and jsonb read as text", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    Handle<> h(pool);
    const auto r = h.query(
        "select ?::uuid, ?::date, ?::timestamp, ?::jsonb->>'k'",
        std::string("7f1c7a5e-3c1d-4c7a-9a55-0d8f8a7e2b11"), std::string("2026-11-05"),
        std::string("2026-11-05 12:34:56"), std::string(R"({"k":"v"})"));
    CHECK(r[0][0] == "7f1c7a5e-3c1d-4c7a-9a55-0d8f8a7e2b11");
    CHECK(r[0][1] == "2026-11-05");
    CHECK(r[0][2] == "2026-11-05 12:34:56");
    CHECK(r[0][3] == "v");
}

TEST_CASE("postgres: ? in literals, comments and operators is not a placeholder", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    Handle<> h(pool);

    auto r = h.query("select 'what?', ?::text", std::string("x"));
    CHECK(r[0][0] == "what?");
    CHECK(r[0][1] == "x");

    r = h.query(R"(select '{"a":1}'::jsonb ?? 'a', ?::int)", 7);
    CHECK(r[0][0] == "t");
    CHECK(r[0][1] == "7");

    r = h.query("select /* why? */ $$a?b$$, ?::text -- really?\n", std::string("y"));
    CHECK(r[0][0] == "a?b");
    CHECK(r[0][1] == "y");

    // No arguments means verbatim, so a lone ? stays the jsonb operator.
    CHECK(h.query(R"(select '{"a":1}'::jsonb ? 'a')")[0][0] == "t");
}

TEST_CASE("postgres: a string_view that is not NUL-terminated stops at its end", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    Handle<> h(pool);
    const std::string s = "select 1;garbage";
    CHECK(h.query(std::string_view(s).substr(0, 8))[0][0] == "1");
}

TEST_CASE("postgres: Transaction commits and rolls back", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    Handle<Write> h(pool);
    h.exec("drop table if exists e2e_tx");
    h.exec("create table e2e_tx (x int)");

    {
        Transaction tx(h);
        h.exec("insert into e2e_tx values (?)", 1);
        tx.commit();
    }
    CHECK(h.query("select count(*) from e2e_tx")[0][0] == "1");

    {
        Transaction tx(h);
        h.exec("insert into e2e_tx values (?)", 2);
    }
    CHECK(h.query("select count(*) from e2e_tx")[0][0] == "1");

    // A failed statement aborts the transaction, and scope exit still cleans up.
    {
        Transaction tx(h);
        CHECK_THROWS_AS(h.exec("insert into e2e_tx values (?)", std::string("not an int")), std::runtime_error);
    }
    CHECK(h.query("select count(*) from e2e_tx")[0][0] == "1");
    h.exec("drop table e2e_tx");
}

TEST_CASE("postgres: an open transaction does not reach the next borrower", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    {
        Handle<Write> w(pool);
        w.exec("drop table if exists e2e_leak");
        w.exec("BEGIN");
        w.exec("create table e2e_leak (x int)");
    }
    Handle<> h(pool);
    CHECK(h.query("select to_regclass('e2e_leak') is null")[0][0] == "t");
}

TEST_CASE("postgres: the pool recovers after a connection is lost", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    {
        Handle<> h(pool);
        CHECK_THROWS_AS(h.query("select pg_terminate_backend(pg_backend_pid())"), std::runtime_error);
    }
    Handle<> h(pool);
    CHECK(h.query("select 1")[0][0] == "1");
}

TEST_CASE("postgres: a lost connection reports a real error message", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    Handle<> h(pool);
    std::string message;
    try {
        h.query("select pg_terminate_backend(pg_backend_pid())");
    } catch (const std::runtime_error& e) {
        message = e.what();
    }
    CHECK_FALSE(message.empty());
}

TEST_CASE("postgres: a failed connect throws", "[postgres]") {
    REQUIRE_PG();
    CHECK_THROWS_AS(Pool("host=127.0.0.1 port=1 connect_timeout=2", 2), std::runtime_error);
}

TEST_CASE("postgres: async variants run and propagate errors", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 1);
    std::string value;
    std::optional<std::string> nullable = "unset";
    std::string count;
    bool threw = false;

    run_task([&]() -> web::Task<void> {
        Handle<Write> h(pool);
        co_await h.async_exec("drop table if exists e2e_async");
        co_await h.async_exec("create table e2e_async (x int)");
        co_await h.async_exec("insert into e2e_async values (?)", 5);
        value = (co_await h.async_query("select x from e2e_async where x = ?", 5))[0][0];
        nullable = (co_await h.async_query_nullable("select null::text"))[0][0];
        count = (co_await h.async_query("select count(*) from e2e_async"))[0][0];
        try {
            co_await h.async_query("select * from e2e_no_such_table");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        co_await h.async_exec("drop table e2e_async");
    });

    CHECK(value == "5");
    CHECK_FALSE(nullable.has_value());
    CHECK(count == "1");
    CHECK(threw);
}

TEST_CASE("postgres: concurrent readers and writers lose nothing", "[postgres]") {
    REQUIRE_PG();
    Pool pool(test_url(), 4);
    {
        Handle<Write> w(pool);
        w.exec("drop table if exists e2e_load");
        w.exec("create table e2e_load (x int)");
    }

    constexpr int kThreads = 8;
    constexpr int kPerThread = 200;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kPerThread; ++i) {
                try {
                    if (t % 2 == 0) {
                        Handle<Write> w(pool);
                        w.exec("insert into e2e_load values (?)", i);
                    } else {
                        Handle<> r(pool);
                        if (r.query("select ?::int", i)[0][0] != std::to_string(i)) {
                            ++failures;
                        }
                    }
                } catch (const std::exception&) {
                    ++failures;
                }
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    CHECK(failures.load() == 0);
    Handle<Write> w(pool);
    CHECK(w.query("select count(*) from e2e_load")[0][0] == std::to_string((kThreads / 2) * kPerThread));
    w.exec("drop table e2e_load");
}
