// Tests for the `Secure` cookie attribute toggle.
// The Set-Cookie line in revet/web.hpp::write_response includes `; Secure`
// when web::config::secure_cookies() is true.
//
// We don't drive a full TCP server here. Instead we exercise the same
// formatting branch via a tiny helper that mirrors the production code
// path — the goal is to lock the contract: opt-in default, env-flag opt-in
// path, runtime toggle.
#include <catch2/catch_test_macros.hpp>
#include <revet/web.hpp>
#include <string>

// Mirror of the production formatting branch in write_response(). If the
// production code drifts, this helper must drift with it — otherwise the
// tests would be lying. Keep the strings in lockstep.
static std::string format_cookie(const std::string& name, const std::string& value) {
    const bool secure = web::config::secure_cookies();
    const std::string_view suffix = secure ? "; Secure" : "";
    if (value.empty()) {
        return std::format("Set-Cookie: {}=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0{}",
                           name, suffix);
    }
    return std::format("Set-Cookie: {}={}; Path=/; HttpOnly; SameSite=Lax{}",
                       name, value, suffix);
}

TEST_CASE("cookies: Secure flag off by default (dev)", "[cookies]") {
    // Snapshot + restore so this test doesn't pollute global state.
    const bool prev = web::config::secure_cookies();
    web::config::secure_cookies(false);
    REQUIRE(format_cookie("k", "v").find("Secure") == std::string::npos);
    web::config::secure_cookies(prev);
}

TEST_CASE("cookies: Secure flag emitted when opted in", "[cookies]") {
    const bool prev = web::config::secure_cookies();
    web::config::secure_cookies(true);
    const auto line = format_cookie("k", "v");
    REQUIRE(line.find("; Secure") != std::string::npos);
    // Sanity: HttpOnly + SameSite still present.
    REQUIRE(line.find("HttpOnly") != std::string::npos);
    REQUIRE(line.find("SameSite=Lax") != std::string::npos);
    web::config::secure_cookies(prev);
}

TEST_CASE("cookies: Secure flag also applied to clear-cookie lines", "[cookies]") {
    // A logout / state-clear emits an empty-value Set-Cookie with Max-Age=0.
    // That line must still carry Secure when configured, otherwise the
    // browser may keep the secure cookie alive in a downgrade scenario.
    const bool prev = web::config::secure_cookies();
    web::config::secure_cookies(true);
    const auto line = format_cookie("k", "");
    REQUIRE(line.find("Max-Age=0") != std::string::npos);
    REQUIRE(line.find("; Secure")  != std::string::npos);
    web::config::secure_cookies(prev);
}
