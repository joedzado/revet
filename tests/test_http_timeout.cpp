#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <netinet/in.h>
#include <revet/http_client.hpp>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

// A server that accepts connections and never answers.
TEST_CASE("CurlClient honors its total timeout", "[http]") {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    REQUIRE(listen(fd, 1) == 0);
    socklen_t len = sizeof(addr);
    REQUIRE(getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);

    http::CurlClient client(http::CurlClient::Options{.total_timeout = std::chrono::seconds{1}});
    const auto start = std::chrono::steady_clock::now();
    const auto response = client.execute("GET", "http://127.0.0.1:" + std::to_string(ntohs(addr.sin_port)) + "/", {}, "");
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(response.status_code == 0);
    CHECK(elapsed < std::chrono::seconds{5});
    close(fd);
}
