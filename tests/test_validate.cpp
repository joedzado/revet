#include <catch2/catch_test_macros.hpp>
#include <revet/validate.hpp>

static web::ParamMap make_map(std::initializer_list<std::pair<std::string, std::string>> pairs) {
    web::ParamMap m;
    for (const auto& [k, v] : pairs) {
        m.data[k] = v;
    }
    return m;
}

// ── required ─────────────────────────────────────────────────────────────────

TEST_CASE("required — passes when field present", "[validate]") {
    auto s = validate::schema(validate::field("name", validate::required));
    auto r = validate::check(make_map({{"name", "Alice"}}), s);
    CHECK(r.has_value());
}

TEST_CASE("required — fails when field missing", "[validate]") {
    auto s = validate::schema(validate::field("name", validate::required));
    auto r = validate::check(make_map({}), s);
    CHECK(!r);
    CHECK(r.error()[0].field == "name");
    CHECK(r.error()[0].message == "required");
}

TEST_CASE("required — fails when field empty", "[validate]") {
    auto s = validate::schema(validate::field("name", validate::required));
    auto r = validate::check(make_map({{"name", ""}}), s);
    CHECK(!r);
}

// ── min_len / max_len ─────────────────────────────────────────────────────────

TEST_CASE("min_len — passes when long enough", "[validate]") {
    auto s = validate::schema(validate::field("pw", validate::min_len<8>));
    auto r = validate::check(make_map({{"pw", "password"}}), s);
    CHECK(r.has_value());
}

TEST_CASE("min_len — fails when too short", "[validate]") {
    auto s = validate::schema(validate::field("pw", validate::min_len<8>));
    auto r = validate::check(make_map({{"pw", "abc"}}), s);
    CHECK(!r);
    CHECK(r.error()[0].message == "min length 8");
}

TEST_CASE("max_len — fails when too long", "[validate]") {
    auto s = validate::schema(validate::field("bio", validate::max_len<10>));
    auto r = validate::check(make_map({{"bio", "this is way too long"}}), s);
    CHECK(!r);
    CHECK(r.error()[0].message == "max length 10");
}

// ── regex ─────────────────────────────────────────────────────────────────────

TEST_CASE("regex — valid email passes", "[validate]") {
    auto s = validate::schema(validate::field("email", validate::regex<"[^@]+@[^@]+\\.[a-z]+">));
    auto r = validate::check(make_map({{"email", "user@example.com"}}), s);
    CHECK(r.has_value());
}

TEST_CASE("regex — invalid email fails", "[validate]") {
    auto s = validate::schema(validate::field("email", validate::regex<"[^@]+@[^@]+\\.[a-z]+">));
    auto r = validate::check(make_map({{"email", "notanemail"}}), s);
    CHECK(!r);
    CHECK(r.error()[0].field == "email");
}

TEST_CASE("regex — digits only", "[validate]") {
    auto s = validate::schema(validate::field("age", validate::regex<"[0-9]+">));
    CHECK(validate::check(make_map({{"age", "42"}}), s).has_value());
    CHECK(!validate::check(make_map({{"age", "abc"}}), s));
}

// ── multiple rules on one field ───────────────────────────────────────────────

TEST_CASE("multiple rules — all pass", "[validate]") {
    auto s = validate::schema(
        validate::field("email", validate::required, validate::max_len<254>, validate::regex<"[^@]+@[^@]+\\.[a-z]+">));
    auto r = validate::check(make_map({{"email", "joe@example.com"}}), s);
    CHECK(r.has_value());
}

TEST_CASE("multiple rules — collects all errors", "[validate]") {
    auto s = validate::schema(validate::field("pw", validate::min_len<8>, validate::max_len<4>));
    auto r = validate::check(make_map({{"pw", "abcde"}}), s);
    CHECK(r.error().size() == 2);
}

// ── multiple fields ───────────────────────────────────────────────────────────

TEST_CASE("multiple fields — errors from both reported", "[validate]") {
    auto s =
        validate::schema(validate::field("name", validate::required), validate::field("email", validate::required));
    auto r = validate::check(make_map({}), s);
    CHECK(r.error().size() == 2);
    CHECK(r.error()[0].field == "name");
    CHECK(r.error()[1].field == "email");
}
