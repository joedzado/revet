#include <catch2/catch_test_macros.hpp>
#include <revet/svg.hpp>


TEST_CASE("svg leaves self-close, so siblings don't nest", "[svg]") {
    CHECK(std::string(svg::path{svg::attr.d("M0 0")}) == "<path d=\"M0 0\"/>");
    CHECK(std::string(svg::line{svg::attr.x1("0")}) == "<line x1=\"0\"/>");
    CHECK(std::string(svg::rect{}) == "<rect/>");
}
