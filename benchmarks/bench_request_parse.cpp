#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <revet/web.hpp>
#include <string>
#include <string_view>

// Benchmark the internal parsing functions directly (no network I/O).
// parse_request() needs a live socket fd, so we benchmark the sub-functions.

static const std::string get_request = "GET /users/42/posts?page=1&limit=10 HTTP/1.1\r\n"
                                       "Host: localhost:8080\r\n"
                                       "Accept: text/html,application/xhtml+xml\r\n"
                                       "Accept-Language: en-US,en;q=0.9\r\n"
                                       "Accept-Encoding: gzip, deflate\r\n"
                                       "Connection: keep-alive\r\n"
                                       "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)\r\n"
                                       "Cookie: session=abc123; theme=dark; lang=en\r\n"
                                       "\r\n";

static const std::string post_request = "POST /api/users HTTP/1.1\r\n"
                                        "Host: localhost:8080\r\n"
                                        "Content-Type: application/x-www-form-urlencoded\r\n"
                                        "Content-Length: 45\r\n"
                                        "Accept: */*\r\n"
                                        "Connection: close\r\n"
                                        "Cookie: session=xyz789; csrftoken=abc\r\n"
                                        "\r\n"
                                        "name=Alice+Smith&email=alice%40example.com&age=30";

static const std::string deep_route = "/users/42/posts/17/comments/99/replies/1";
static const std::string shallow_route = "/about";

TEST_CASE("request parsing benchmarks", "[bench][request]") {
    BENCHMARK("match_route: static path /about") {
        web::ParamMap params;
        return web::match_route("/about", "/about", params);
    };

    BENCHMARK("match_route: 4-segment path with 2 params") {
        web::ParamMap params;
        return web::match_route("/users/:id/posts/:post_id", "/users/42/posts/17", params);
    };

    BENCHMARK("match_route: 8-segment deep path with 4 params") {
        web::ParamMap params;
        return web::match_route("/users/:uid/posts/:pid/comments/:cid/replies/:rid",
                                "/users/42/posts/17/comments/99/replies/1", params);
    };

    BENCHMARK("parse_params: simple query string (5 pairs)") {
        return web::parse_params("page=1&limit=10&sort=date&order=desc&filter=active");
    };

    BENCHMARK("parse_params: url-encoded query string") {
        return web::parse_params("name=Alice+Smith&email=alice%40example.com"
                                 "&redirect=%2Fusers%2F42&msg=hello+world%21");
    };

    BENCHMARK("parse_params: form body (6 fields)") {
        return web::parse_params("name=Alice+Smith&email=alice%40example.com"
                                 "&age=30&city=New+York&country=US&agree=on");
    };

    BENCHMARK("parse_params: cookie string (5 cookies, no url_decode)") {
        return web::parse_params("session=abc123; theme=dark; lang=en; csrf=xyz789; user_id=42", ';', false);
    };

    // Measure the header section parsing sub-components
    BENCHMARK("string find for header terminator (small request)") {
        return get_request.find("\r\n\r\n");
    };

    BENCHMARK("string find for header terminator (large request, 8KB)") {
        // Simulates a request with many headers
        std::string large_req;
        large_req.reserve(8192);
        large_req += "GET / HTTP/1.1\r\n";
        for (int i = 0; i < 50; ++i) {
            large_req += std::format("X-Custom-Header-{}: value-number-{}\r\n", i, i);
        }
        large_req += "\r\n";
        return large_req.find("\r\n\r\n");
    };

    BENCHMARK("parse_params: large form body (20 fields)") {
        std::string body;
        body.reserve(512);
        for (int i = 0; i < 20; ++i) {
            if (i > 0) {
                body += '&';
            }
            body += std::format("field{}=value{}", i, i);
        }
        return web::parse_params(body);
    };
}
