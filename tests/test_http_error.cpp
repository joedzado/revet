// Tests for http::Error and http::Response::ok() — the error surface
// that generated apis.hpp code relies on.
#include <catch2/catch_test_macros.hpp>
#include <revet/http_client.hpp>
#include <stdexcept>
#include <string>

TEST_CASE("Response::ok() true for 2xx", "[http]") {
    REQUIRE(http::Response{200, "", {}}.ok());
    REQUIRE(http::Response{201, "", {}}.ok());
    REQUIRE(http::Response{299, "", {}}.ok());
}

TEST_CASE("Response::ok() false for non-2xx and transport failure", "[http]") {
    REQUIRE_FALSE(http::Response{0,   "", {}}.ok());  // transport failure
    REQUIRE_FALSE(http::Response{199, "", {}}.ok());
    REQUIRE_FALSE(http::Response{300, "", {}}.ok());
    REQUIRE_FALSE(http::Response{404, "", {}}.ok());
    REQUIRE_FALSE(http::Response{500, "", {}}.ok());
}

TEST_CASE("operator bool mirrors ok()", "[http]") {
    REQUIRE(static_cast<bool>(http::Response{200, "", {}}));
    REQUIRE_FALSE(static_cast<bool>(http::Response{0, "", {}}));
    REQUIRE_FALSE(static_cast<bool>(http::Response{404, "", {}}));
}

TEST_CASE("http::Error stores HTTP status + body", "[http]") {
    http::Error e(404, R"({"message":"Not Found"})");
    REQUIRE(e.status_code == 404);
    REQUIRE(e.body == R"({"message":"Not Found"})");
    const std::string what = e.what();
    REQUIRE(what.find("404") != std::string::npos);
    REQUIRE(what.find("Not Found") != std::string::npos);
}

TEST_CASE("http::Error for transport failure uses status 0", "[http]") {
    http::Error e(0, "Curl error: Could not resolve host");
    REQUIRE(e.status_code == 0);
    const std::string what = e.what();
    REQUIRE(what.find("transport") != std::string::npos);
    REQUIRE(what.find("Could not resolve host") != std::string::npos);
    // Note: must NOT contain "0:" — that would imply 0 was a real HTTP code.
    REQUIRE(what.find("http error 0") == std::string::npos);
}

TEST_CASE("http::Error is catchable as std::runtime_error", "[http]") {
    bool caught_as_runtime = false;
    int  saw_status        = -1;
    try {
        throw http::Error(503, "Service Unavailable");
    } catch (const std::runtime_error& e) {
        caught_as_runtime = true;
        // Downcast to recover status_code, since the consumer might catch
        // either http::Error or its base.
        if (const auto* h = dynamic_cast<const http::Error*>(&e)) {
            saw_status = h->status_code;
        }
    }
    REQUIRE(caught_as_runtime);
    REQUIRE(saw_status == 503);
}

TEST_CASE("http::Error is also catchable directly as http::Error", "[http]") {
    try {
        throw http::Error(429, "rate limited");
        FAIL("did not throw");
    } catch (const http::Error& e) {
        REQUIRE(e.status_code == 429);
        REQUIRE(e.body == "rate limited");
    }
}

TEST_CASE("MockClient returns 404 with no stub — still a Response, not throw",
          "[http][mock]") {
    // Low-level client itself never throws — only the *generated* apis.hpp
    // wrapper translates !ok() into an http::Error throw. This lets advanced
    // users opt out of exceptions by calling http::get(...) directly.
    http::MockClient mock;
    auto resp = mock.execute("GET", "https://example.com/missing", {}, "");
    REQUIRE(resp.status_code == 404);
    REQUIRE_FALSE(resp.ok());
}
