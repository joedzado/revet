#include <catch2/catch_test_macros.hpp>
#include <revet/web.hpp>
#include <string>
#include <sys/socket.h>
#include <vector>

static auto dispatch_and_read(const std::vector<web::Route>& all_routes, web::Request req) -> std::string {
    for (const auto& r : all_routes) {
        if (web::match_route(r.path, req.path.path, req.params)) {
            web::Response resp;
            if (req.method == web::Method::GET && r.get) {
                resp = r.get(req);
            } else if (req.method == web::Method::POST && r.post) {
                resp = r.post(req);
            } else if (req.method == web::Method::PUT && r.put) {
                resp = r.put(req);
            } else if (req.method == web::Method::DEL && r.del) {
                resp = r.del(req);
            } else if (req.method == web::Method::PATCH && r.patch) {
                resp = r.patch(req);
            }
            return resp.body;
        }
    }
    return "404";
}

static auto make_req(web::Method m, std::string path) -> web::Request {
    return {
        .method = m,
        .path = web::Url(std::move(path)),
    };
}

TEST_CASE("Route Dispatching", "[router]") {
    std::vector<web::Route> all_routes;

    SECTION("Static path matching") {
        all_routes.push_back(
            {.path = "/", .get = [](const auto&) -> auto { return web::to_response("home"); }, .post = nullptr});
        all_routes.push_back(
            {.path = "/about", .get = [](const auto&) -> auto { return web::to_response("about"); }, .post = nullptr});

        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/")) == "home");
        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/about")) == "about");
        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/missing")) == "404");
    }

    SECTION("Parameterized path — single segment") {
        all_routes.push_back({
            .path = "/users/:id",
            .get = [](const auto& req) { return web::to_response(req.params.get("id")); },
        });

        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/users/42")) == "42");
        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/users/abc")) == "abc");
        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/users")) == "404");
    }

    SECTION("Parameterized path — multiple segments") {
        all_routes.push_back({
            .path = "/users/:uid/posts/:pid",
            .get =
                [](const auto& req) { return web::to_response(req.params.get("uid") + ":" + req.params.get("pid")); },
        });

        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/users/7/posts/99")) == "7:99");
        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/users/7/posts")) == "404");
    }

    SECTION("Static routes take priority over params") {
        all_routes.push_back({
            .path = "/users/me",
            .get = [](const auto&) { return web::to_response("me"); },
        });
        all_routes.push_back({
            .path = "/users/:id",
            .get = [](const auto& req) { return web::to_response(req.params.get("id")); },
        });

        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/users/me")) == "me");
        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/users/123")) == "123");
    }

    SECTION("Method separation — GET and POST") {
        all_routes.push_back({.path = "/data",
                              .get = [](const auto&) -> auto { return web::to_response("get"); },
                              .post = [](const auto&) -> auto { return web::to_response("post"); }});

        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::GET, "/data")) == "get");
        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::POST, "/data")) == "post");
    }

    SECTION("PUT, DELETE, PATCH handlers") {
        all_routes.push_back({
            .path = "/items/:id",
            .put = [](const auto& req) { return web::to_response("put:" + req.params.get("id")); },
            .del = [](const auto& req) { return web::to_response("del:" + req.params.get("id")); },
            .patch = [](const auto& req) { return web::to_response("patch:" + req.params.get("id")); },
        });

        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::PUT, "/items/7")) == "put:7");
        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::DEL, "/items/7")) == "del:7");
        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::PATCH, "/items/7")) == "patch:7");
    }

    SECTION("Missing verb returns empty body") {
        all_routes.push_back({
            .path = "/resource",
            .get = [](const auto&) -> auto { return web::to_response("get"); },
        });

        // POST to a GET-only route — no handler, empty body
        REQUIRE(dispatch_and_read(all_routes, make_req(web::Method::POST, "/resource")).empty());
    }
}
