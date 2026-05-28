// Tests for web::crypto::secure_eq — constant-time string comparison used
// throughout the framework for secret/token comparisons.
//
// Note: a unit test cannot prove constant-time behavior (that's a property
// of the implementation, not observable from the outside on a single
// machine). What we *can* verify is the functional contract: that the
// function behaves as a string-equality predicate.
#include <catch2/catch_test_macros.hpp>
#include <revet/crypto.hpp>
#include <string>

using web::crypto::secure_eq;

TEST_CASE("secure_eq: identical strings are equal", "[crypto]") {
    REQUIRE(secure_eq("abc", "abc"));
    REQUIRE(secure_eq("", ""));
    REQUIRE(secure_eq(std::string(1024, 'x'), std::string(1024, 'x')));
}

TEST_CASE("secure_eq: same length but different content is unequal", "[crypto]") {
    REQUIRE_FALSE(secure_eq("abc", "abd"));
    REQUIRE_FALSE(secure_eq("token123", "token124"));
}

TEST_CASE("secure_eq: different lengths are unequal", "[crypto]") {
    REQUIRE_FALSE(secure_eq("abc",  "abcd"));
    REQUIRE_FALSE(secure_eq("abcd", "abc"));
    REQUIRE_FALSE(secure_eq("",     "x"));
    REQUIRE_FALSE(secure_eq("x",    ""));
}

TEST_CASE("secure_eq: handles embedded null bytes", "[crypto]") {
    using namespace std::string_literals;
    REQUIRE(secure_eq("a\0b"s, "a\0b"s));
    REQUIRE_FALSE(secure_eq("a\0b"s, "a\0c"s));
}
