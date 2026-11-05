#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <revet/db/pg_params.hpp>
#include <string>

using db::postgres::detail::to_pg_param;

TEST_CASE("to_pg_param: integers") {
    CHECK(to_pg_param(0) == "0");
    CHECK(to_pg_param(7) == "7");
    CHECK(to_pg_param(-42) == "-42");

    const int32_t i32 = 2'147'483'647;
    CHECK(to_pg_param(i32) == "2147483647");

    const int64_t big = 9'000'000'000LL;
    CHECK(to_pg_param(big) == "9000000000");

    const int64_t neg = -9'000'000'000LL;
    CHECK(to_pg_param(neg) == "-9000000000");
}

TEST_CASE("to_pg_param: unsigned integers") {
    const uint32_t u = 4'000'000'000U;
    CHECK(to_pg_param(u) == "4000000000");
}

TEST_CASE("to_pg_param: floating point round-trips") {
    // Value must survive string → postgres → back
    auto rt = [](double v) { return std::stod(to_pg_param(v)); };

    CHECK(rt(0.0) == 0.0);
    CHECK(rt(3.14) == 3.14);
    CHECK(rt(-2.5) == -2.5);
    CHECK(rt(1e100) == 1e100);

    // Smallest normal and denorm
    CHECK(rt(std::numeric_limits<double>::min()) == std::numeric_limits<double>::min());
    CHECK(rt(std::numeric_limits<double>::epsilon()) == std::numeric_limits<double>::epsilon());
}

TEST_CASE("to_pg_param: float round-trips") {
    const float f = 3.5F;
    CHECK(std::stof(to_pg_param(f)) == f);
}

TEST_CASE("to_pg_param: bool") {
    CHECK(to_pg_param(true) == "true");
    CHECK(to_pg_param(false) == "false");
}

TEST_CASE("to_pg_param: std::string passthrough") {
    const std::string s = "hello world";
    CHECK(to_pg_param(s) == "hello world");
    CHECK(to_pg_param(std::string{}) == "");
}

TEST_CASE("to_pg_param: string_view passthrough") {
    using namespace std::string_view_literals;
    CHECK(to_pg_param("needle"sv) == "needle");
}

TEST_CASE("to_pg_param: const char* passthrough") {
    CHECK(to_pg_param("literal") == "literal");
}

using db::postgres::detail::positional_sql;
using db::postgres::detail::to_nullable_pg_param;

TEST_CASE("to_nullable_pg_param: NULL sources") {
    CHECK_FALSE(to_nullable_pg_param(nullptr).has_value());
    CHECK_FALSE(to_nullable_pg_param(std::optional<int>{}).has_value());
    CHECK(to_nullable_pg_param(std::optional<int>{4}) == std::optional<std::string>("4"));
    CHECK(to_nullable_pg_param(std::string("")) == std::optional<std::string>(""));
}

TEST_CASE("positional_sql: placeholders") {
    CHECK(positional_sql("select ?, ?") == "select $1, $2");
    CHECK(positional_sql("no params") == "no params");
    CHECK(positional_sql("") == "");
}

TEST_CASE("positional_sql: ? is left alone inside quoted text and comments") {
    CHECK(positional_sql("select '?', ?") == "select '?', $1");
    CHECK(positional_sql("select 'it''s ?', ?") == "select 'it''s ?', $1");
    CHECK(positional_sql(R"(select "a?b", ?)") == R"(select "a?b", $1)");
    CHECK(positional_sql(R"(select E'\'?', ?)") == R"(select E'\'?', $1)");
    CHECK(positional_sql("select 1 -- ?\n, ?") == "select 1 -- ?\n, $1");
    CHECK(positional_sql("select /* ? /* ? */ ? */ ?") == "select /* ? /* ? */ ? */ $1");
    CHECK(positional_sql("select $$?$$, ?") == "select $$?$$, $1");
    CHECK(positional_sql("select $tag$ ? $$ ? $tag$, ?") == "select $tag$ ? $$ ? $tag$, $1");
}

TEST_CASE("positional_sql: ?? is a literal ?") {
    CHECK(positional_sql("select j ?? 'k', ?") == "select j ? 'k', $1");
}

TEST_CASE("positional_sql: a backslash only escapes in E strings") {
    CHECK(positional_sql(R"(select '\', ?)") == R"(select '\', $1)");
    CHECK(positional_sql(R"(select name'\', ?)") == R"(select name'\', $1)");
}

TEST_CASE("positional_sql: explicit $n and unterminated text do not crash") {
    CHECK(positional_sql("select $1") == "select $1");
    CHECK(positional_sql("select 'open ?") == "select 'open ?");
    CHECK(positional_sql("select /* open ?") == "select /* open ?");
    CHECK(positional_sql("select $tag$ open ?") == "select $tag$ open ?");
}
