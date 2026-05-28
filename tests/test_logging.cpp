#include <catch2/catch_test_macros.hpp>
#include <revet/log.hpp>

TEST_CASE("Logging system level control", "[logging]") {
    log::set_level(log::Level::DEBUG);
    REQUIRE(log::get_level() == log::Level::DEBUG);

    log::set_level(log::Level::INFO);
    REQUIRE(log::get_level() == log::Level::INFO);

    log::set_level(log::Level::ERROR);
    REQUIRE(log::get_level() == log::Level::ERROR);
}

TEST_CASE("Default tag (TU-level)", "[logging]") {
    log::set_level(log::Level::DEBUG);

    // With LOG_TAG macro, current_tag is set
    // These should not throw and use the current tag
    log::debug("debug message");
    log::info("info message");
    log::warn("warn message");
    log::error("error message");

    log::set_level(log::Level::INFO);
}

TEST_CASE("Explicit tag override (compiler-checked)", "[logging]") {
    log::set_level(log::Level::DEBUG);

    // Use declared tags explicitly (requires format arguments)
    log::debug<log::tags::FRAMEWORK>("framework {}", "message");
    log::info<log::tags::ROUTES>("routes {}", "message");
    log::warn<log::tags::DB>("db {}", "message");
    log::error<log::tags::APP>("app {}", "message");

    // Formatted strings with explicit tags
    log::info<log::tags::ROUTES>("route: {}", "/api/users");
    log::info<log::tags::DB>("query: {}", "SELECT * FROM users");

    log::set_level(log::Level::INFO);
}

TEST_CASE("Log levels filter messages", "[logging]") {
    // When level is INFO, DEBUG should be suppressed
    log::set_level(log::Level::INFO);
    log::debug("this should not appear");
    log::info("this should appear");

    // When level is ERROR, only ERROR should appear
    log::set_level(log::Level::ERROR);
    log::warn("this should not appear");
    log::error("only this appears");

    // Reset
    log::set_level(log::Level::INFO);
}

TEST_CASE("Formatted strings work with all tags", "[logging]") {
    log::set_level(log::Level::DEBUG);

    // Default tag
    log::info("value: {}, id: {}", 42, 123);

    // Explicit tags
    log::info<log::tags::ROUTES>("method: {}, path: {}", "GET", "/users");
    log::info<log::tags::DB>("rows: {}, time_ms: {}", 100, 45);

    log::set_level(log::Level::INFO);
}
