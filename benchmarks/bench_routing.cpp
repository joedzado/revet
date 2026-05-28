#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <revet/web.hpp>

// Simulates a realistic route table with 30 routes of various complexity.
static const std::vector<std::string> routes = {
    "/",
    "/about",
    "/contact",
    "/login",
    "/logout",
    "/register",
    "/api/health",
    "/api/version",
    "/users",
    "/users/:id",
    "/users/:id/profile",
    "/users/:id/settings",
    "/users/:id/posts",
    "/users/:id/posts/:post_id",
    "/admin",
    "/admin/users",
    "/admin/users/:id",
    "/admin/settings",
    "/blog",
    "/blog/:slug",
    "/blog/:slug/comments",
    "/blog/:slug/comments/:comment_id",
    "/products",
    "/products/:sku",
    "/products/:sku/reviews",
    "/cart",
    "/cart/:item_id",
    "/orders",
    "/orders/:order_id",
    "/orders/:order_id/items",
};

// Paths that match — some early, some late, one that hits 404.
static const std::vector<std::string> test_paths = {
    "/about",
    "/users/42",
    "/users/42/posts/17",
    "/blog/my-great-post/comments/99",
    "/orders/ABC123/items",
    "/nonexistent/path/that/404s",
};

TEST_CASE("routing benchmarks", "[bench][routing]") {
    BENCHMARK("match_route: static segment (early match)") {
        web::ParamMap params;
        return web::match_route("/about", "/about", params);
    };

    BENCHMARK("match_route: single param") {
        web::ParamMap params;
        return web::match_route("/users/:id", "/users/42", params);
    };

    BENCHMARK("match_route: two params (deep)") {
        web::ParamMap params;
        return web::match_route("/users/:id/posts/:post_id", "/users/42/posts/17", params);
    };

    BENCHMARK("match_route: no match (different segment count)") {
        web::ParamMap params;
        return web::match_route("/users/:id", "/nonexistent/path/that/404s", params);
    };

    BENCHMARK("linear route scan: early hit (route 2/30)") {
        web::ParamMap params;
        for (const auto& route : routes) {
            if (web::match_route(route, "/about", params)) {
                break;
            }
        }
        return params;
    };

    BENCHMARK("linear route scan: mid hit (route 14/30 with 2 params)") {
        web::ParamMap params;
        for (const auto& route : routes) {
            if (web::match_route(route, "/users/42/posts/17", params)) {
                break;
            }
        }
        return params;
    };

    BENCHMARK("linear route scan: late hit (route 30/30 with 2 params)") {
        web::ParamMap params;
        for (const auto& route : routes) {
            if (web::match_route(route, "/orders/ABC123/items", params)) {
                break;
            }
        }
        return params;
    };

    BENCHMARK("linear route scan: full miss (30 checks)") {
        web::ParamMap params;
        for (const auto& route : routes) {
            web::match_route(route, "/nonexistent/path/that/404s", params);
        }
        return params;
    };

    BENCHMARK("parse_params: query string 5 pairs") {
        return web::parse_params("name=Alice&age=30&city=NYC&lang=en&theme=dark");
    };

    BENCHMARK("parse_params: query string with url-encoded values") {
        return web::parse_params("q=hello%20world&redirect=%2Fusers%2F42&msg=it%27s+great");
    };
}
