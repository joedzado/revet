#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <revet/web.hpp>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

TEST_CASE("Multithreaded request handling", "[threading]") {
    std::vector<web::Route> all_routes;
    all_routes.push_back(
        {.path = "/", .get = [](const auto&) -> web::Response { return web::to_response("ThreadSafe"); }, .post = {}});

    auto const handle_req = [&](int fd) -> void {
        auto const req = web::parse_request(fd);
        for (const auto& r : all_routes) {
            if (r.path == req.path.path) {
                web::send_response(fd, r.get(req));
                return;
            }
        }
        web::send_response(fd, {.body = "404", .location = {}, .cookies = {}, .status = web::status::not_found});
    };

    SECTION("Concurrent requests on same router") {
        const int num_threads = 10;
        std::atomic<int> successes{0};
        std::vector<std::jthread> workers;
        workers.reserve(static_cast<size_t>(num_threads));

        for (int i = 0; i < num_threads; ++i) {
            workers.emplace_back([&]() -> void {
                std::array<int, 2> fds{};
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) < 0) {
                    return;
                }

                const char* raw = "GET / HTTP/1.1\r\n\r\n";
                send(fds[1] /* NOLINT */, raw, strlen(raw), 0);

                handle_req(fds[0] /* NOLINT */);

                std::array<char, 1024> buf{};
                ssize_t const n = recv(fds[1] /* NOLINT */, buf.data(), buf.size(), 0);
                std::string const res(buf.data(), static_cast<size_t>(n));

                if (res.find("ThreadSafe") != std::string::npos) {
                    ++successes;
                }

                close(fds[0] /* NOLINT */);
                close(fds[1] /* NOLINT */);
            });
        }

        workers.clear(); // joins all

        REQUIRE(successes == num_threads);
    }
}
