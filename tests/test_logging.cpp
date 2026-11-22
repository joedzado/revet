#include <catch2/catch_test_macros.hpp>
#include <revet/log.hpp>

TEST_CASE("Logging system level control", "[logging]") {
    web::log::set_level(web::log::Level::DEBUG);
    REQUIRE(web::log::get_level() == web::log::Level::DEBUG);

    web::log::set_level(web::log::Level::INFO);
    REQUIRE(web::log::get_level() == web::log::Level::INFO);

    web::log::set_level(web::log::Level::ERROR);
    REQUIRE(web::log::get_level() == web::log::Level::ERROR);
}

TEST_CASE("Default tag (TU-level)", "[logging]") {
    web::log::set_level(web::log::Level::DEBUG);

    // With LOG_TAG macro, current_tag() is set
    // These should not throw and use the current tag
    web::log::debug("debug message");
    web::log::info("info message");
    web::log::warn("warn message");
    web::log::error("error message");

    web::log::set_level(web::log::Level::INFO);
}

TEST_CASE("Explicit tag override (compiler-checked)", "[logging]") {
    web::log::set_level(web::log::Level::DEBUG);

    // Use declared tags explicitly (requires format arguments)
    web::log::debug<web::log::tags::FRAMEWORK>("framework {}", "message");
    web::log::info<web::log::tags::ROUTES>("routes {}", "message");
    web::log::warn<web::log::tags::DB>("db {}", "message");
    web::log::error<web::log::tags::APP>("app {}", "message");

    // Formatted strings with explicit tags
    web::log::info<web::log::tags::ROUTES>("route: {}", "/api/users");
    web::log::info<web::log::tags::DB>("query: {}", "SELECT * FROM users");

    web::log::set_level(web::log::Level::INFO);
}

TEST_CASE("Log levels filter messages", "[logging]") {
    // When level is INFO, DEBUG should be suppressed
    web::log::set_level(web::log::Level::INFO);
    web::log::debug("this should not appear");
    web::log::info("this should appear");

    // When level is ERROR, only ERROR should appear
    web::log::set_level(web::log::Level::ERROR);
    web::log::warn("this should not appear");
    web::log::error("only this appears");

    // Reset
    web::log::set_level(web::log::Level::INFO);
}

TEST_CASE("Formatted strings work with all tags", "[logging]") {
    web::log::set_level(web::log::Level::DEBUG);

    // Default tag
    web::log::info("value: {}, id: {}", 42, 123);

    // Explicit tags
    web::log::info<web::log::tags::ROUTES>("method: {}, path: {}", "GET", "/users");
    web::log::info<web::log::tags::DB>("rows: {}, time_ms: {}", 100, 45);

    web::log::set_level(web::log::Level::INFO);
}
