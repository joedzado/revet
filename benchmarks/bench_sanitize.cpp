#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <revet/sanitize.hpp>
#include <string>

static const std::string small_plain = "Hello, World! This is a test.";
static const std::string medium_plain(1024, 'A');
static const std::string large_plain(64 * 1024, 'B');

static const std::string small_html = "Hello <b>World</b> &amp; \"friends\" it's great";
static const std::string medium_html = []() {
    std::string s;
    s.reserve(1024);
    for (int i = 0; i < 100; ++i) {
        s += "Hello <b>World</b> & 'test' ";
    }
    return s;
}();

static const std::string small_b64 = sanitize::base64_encode(small_plain);
static const std::string medium_b64 = sanitize::base64_encode(medium_plain);
static const std::string large_b64 = sanitize::base64_encode(large_plain);

TEST_CASE("sanitize benchmarks", "[bench][sanitize]") {
    BENCHMARK("html_escape small (30 bytes, no specials)") {
        return sanitize::html_escape(small_plain);
    };

    BENCHMARK("html_escape medium with specials (1KB)") {
        return sanitize::html_escape(medium_html);
    };

    BENCHMARK("base64_encode small (30 bytes)") {
        return sanitize::base64_encode(small_plain);
    };

    BENCHMARK("base64_encode medium (1KB)") {
        return sanitize::base64_encode(medium_plain);
    };

    BENCHMARK("base64_encode large (64KB)") {
        return sanitize::base64_encode(large_plain);
    };

    BENCHMARK("base64_decode small") {
        return sanitize::base64_decode(small_b64);
    };

    BENCHMARK("base64_decode medium (1KB)") {
        return sanitize::base64_decode(medium_b64);
    };

    BENCHMARK("base64_decode large (64KB)") {
        return sanitize::base64_decode(large_b64);
    };

    BENCHMARK("url_decode no encoding") {
        return sanitize::url_decode("hello_world_no_encoding_needed");
    };

    BENCHMARK("url_decode with percent encoding") {
        return sanitize::url_decode("hello%20world%21%20foo%3Dbar%26baz%3Dqux");
    };
}
