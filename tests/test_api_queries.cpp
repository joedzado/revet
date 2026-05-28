#include <catch2/catch_test_macros.hpp>
#include <revet/http_client.hpp>

// Example test showing how to test API queries with mocks

TEST_CASE("API queries with mock client") {
    auto mock = std::make_shared<http::MockClient>();

    // Stub a successful response
    http::Response success_resp;
    success_resp.status_code = 200;
    success_resp.body = R"({"id": 123, "name": "Alice", "email": "alice@example.com"})";
    mock->stub("GET", "https://api.github.com/users/alice", success_resp);

    // Set the global client to our mock
    http::global::set_client(mock);

    // Simulate an API call (in real code, this would be co_await github_com::user("alice"))
    auto resp = mock->execute("GET", "https://api.github.com/users/alice", {}, "");

    // Assert response
    REQUIRE(resp.status_code == 200);
    REQUIRE(resp.body.find("Alice") != std::string::npos);

    // Check call history
    REQUIRE(mock->history().size() == 1);
    REQUIRE(mock->history()[0].method == "GET");
    REQUIRE(mock->history()[0].url == "https://api.github.com/users/alice");

    mock->clear();
}

TEST_CASE("API queries with error responses") {
    auto mock = std::make_shared<http::MockClient>();

    // Stub an error response
    http::Response error_resp;
    error_resp.status_code = 404;
    error_resp.body = R"({"message": "Not Found"})";
    mock->stub("GET", "https://api.github.com/users/nonexistent", error_resp);

    http::global::set_client(mock);

    auto resp = mock->execute("GET", "https://api.github.com/users/nonexistent", {}, "");

    REQUIRE(resp.status_code == 404);
    REQUIRE(!resp); // operator bool returns false for non-2xx

    mock->clear();
}

TEST_CASE("Multiple API calls in sequence") {
    auto mock = std::make_shared<http::MockClient>();

    // Stub multiple responses
    http::Response user_resp;
    user_resp.status_code = 200;
    user_resp.body = R"({"id": 1, "login": "alice"})";
    mock->stub("GET", "https://api.github.com/users/alice", user_resp);

    http::Response repos_resp;
    repos_resp.status_code = 200;
    repos_resp.body = R"([{"name": "repo1"}, {"name": "repo2"}])";
    mock->stub("GET", "https://api.github.com/users/alice/repos", repos_resp);

    http::global::set_client(mock);

    // Simulate sequential API calls
    auto r1 = mock->execute("GET", "https://api.github.com/users/alice", {}, "");
    auto r2 = mock->execute("GET", "https://api.github.com/users/alice/repos", {}, "");

    REQUIRE(r1.status_code == 200);
    REQUIRE(r2.status_code == 200);

    // Verify call history
    REQUIRE(mock->history().size() == 2);
    REQUIRE(mock->history()[0].url == "https://api.github.com/users/alice");
    REQUIRE(mock->history()[1].url == "https://api.github.com/users/alice/repos");

    mock->clear();
}
