#include <catch2/catch_test_macros.hpp>
#include <revet/web.hpp>

TEST_CASE("Custom Error Handler Registry", "[web]") {
    // Clear existing for clean test
    web::error_handlers().clear();

    SECTION("Register 404 handler") {
        web::on_error(web::status::not_found, [](const web::Request&) {
            auto resp = web::to_response("CUSTOM NOT FOUND");
            resp.status = web::status::not_found;
            return resp;
        });

        REQUIRE(web::error_handlers().contains(404));
        web::Request req;
        auto resp = web::error_handlers().at(404)(req);
        REQUIRE(resp.body == "CUSTOM NOT FOUND");
        REQUIRE(resp.status == web::status::not_found);
    }

    SECTION("Register 500 handler") {
        web::on_error(web::status::internal_error, [](const web::Request&) {
            auto resp = web::to_response("CUSTOM ERROR");
            resp.status = web::status::internal_error;
            return resp;
        });

        REQUIRE(web::error_handlers().contains(500));
        web::Request req;
        auto resp = web::error_handlers().at(500)(req);
        REQUIRE(resp.body == "CUSTOM ERROR");
        REQUIRE(resp.status == web::status::internal_error);
    }
}
