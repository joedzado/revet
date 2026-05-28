#include <catch2/catch_test_macros.hpp>
#include <revet/sanitize.hpp>

// ── safe_url ──────────────────────────────────────────────────────────────────

TEST_CASE("safe_url — passes safe schemes", "[sanitize]") {
    CHECK(sanitize::safe_url("https://example.com") == "https://example.com");
    CHECK(sanitize::safe_url("http://example.com") == "http://example.com");
    CHECK(sanitize::safe_url("/relative/path") == "/relative/path");
    CHECK(sanitize::safe_url("users/123") == "users/123");
    CHECK(sanitize::safe_url("//example.com") == "//example.com");
    CHECK(sanitize::safe_url("?query=1") == "?query=1");
    CHECK(sanitize::safe_url("#anchor") == "#anchor");
    CHECK(sanitize::safe_url("mailto:a@b.com") == "mailto:a@b.com");
    CHECK(sanitize::safe_url("tel:+123456789") == "tel:+123456789");
    CHECK(sanitize::safe_url("") == "");
}

TEST_CASE("safe_url — blocks javascript: URI", "[sanitize]") {
    CHECK(sanitize::safe_url("javascript:alert(1)") == "#");
    CHECK(sanitize::safe_url("JAVASCRIPT:alert(1)") == "#");
    CHECK(sanitize::safe_url("JavaScript:void(0)") == "#");
}

TEST_CASE("safe_url — blocks other executable schemes", "[sanitize]") {
    CHECK(sanitize::safe_url("data:text/html,<script>") == "#");
    CHECK(sanitize::safe_url("vbscript:msgbox(1)") == "#");
}

TEST_CASE("safe_url — strips leading whitespace before scheme check", "[sanitize]") {
    CHECK(sanitize::safe_url("  javascript:alert(1)") == "#");
    CHECK(sanitize::safe_url("\tjavascript:alert(1)") == "#");
    CHECK(sanitize::safe_url("  https://example.com") == "  https://example.com");
}

// ── url_decode ────────────────────────────────────────────────────────────────

TEST_CASE("url_decode — valid percent-encoding", "[sanitize]") {
    CHECK(sanitize::url_decode("hello%20world") == "hello world");
    CHECK(sanitize::url_decode("a%2Bb") == "a+b");
    CHECK(sanitize::url_decode("%41%42%43") == "ABC");
    CHECK(sanitize::url_decode("+") == " ");
    CHECK(sanitize::url_decode("") == "");
}

TEST_CASE("url_decode — invalid %XX passes through literally, no null byte", "[sanitize]") {
    // %GG is not valid hex — must NOT produce a null byte
    std::string result = sanitize::url_decode("%GG");
    CHECK(result == "%GG");
    CHECK(result.find('\0') == std::string::npos);

    // truncated sequence at end of string
    CHECK(sanitize::url_decode("%4") == "%4");
    CHECK(sanitize::url_decode("%") == "%");

    // partial valid sequence
    CHECK(sanitize::url_decode("%2") == "%2");
}

// ── Base64 ────────────────────────────────────────────────────────────────────

TEST_CASE("Base64 Encoding/Decoding", "[sanitize]") {
    SECTION("Empty string") {
        REQUIRE(sanitize::base64_encode("").empty());
        REQUIRE(sanitize::base64_decode("").empty());
    }

    SECTION("Basic strings") {
        REQUIRE(sanitize::base64_encode("hello") == "aGVsbG8=");
        REQUIRE(sanitize::base64_decode("aGVsbG8=") == "hello");
    }

    SECTION("Binary data") {
        std::string const binary{"\x00\x01\x02\x03\xFF", 5};
        std::string const encoded = sanitize::base64_encode(binary);
        REQUIRE(sanitize::base64_decode(encoded) == binary);
    }
}
