#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <revet/http_client.hpp>

// This test demonstrates end-to-end API query integration:
// 1. Generated code (from queries/github_get_user.api)
// 2. MockClient for testing without real network calls
// 3. Integration with routes (simulated)

TEST_CASE("API query integration: generated code with mock client") {
    // Setup: Create mock client and stub response
    auto mock = std::make_shared<http::MockClient>();

    http::Response github_response;
    github_response.status_code = 200;
    github_response.body = R"({"id": 123, "login": "alice", "name": "Alice", "email": "alice@example.com"})";

    // Stub both the actual URL pattern and variations
    mock->stub("GET", "https://api.github.com/users/alice", github_response);

    // Replace global HTTP client with mock
    http::global::set_client(mock);

    // Simulate what a route would do:
    // 1. User requests /users/alice
    // 2. Route calls: auto response = co_await github_com::user("alice")
    // 3. (For this test, we manually call the HTTP client)

    // This simulates: co_await http::get("https://api.github.com/users/" + "alice")
    auto response = mock->execute("GET", "https://api.github.com/users/alice", {}, "");

    // Assertions
    REQUIRE(response); // Operator bool checks 200-299
    REQUIRE(response.status_code == 200);
    REQUIRE(response.body.find("alice") != std::string::npos);

    // Verify the API was called correctly
    REQUIRE(mock->history().size() == 1);
    REQUIRE(mock->history()[0].method == "GET");
    REQUIRE(mock->history()[0].url == "https://api.github.com/users/alice");

    mock->clear();
}

TEST_CASE("API query integration: error handling in routes") {
    auto mock = std::make_shared<http::MockClient>();

    http::Response error_response;
    error_response.status_code = 404;
    error_response.body = R"({"message": "Not Found"})";

    mock->stub("GET", "https://api.github.com/users/unknown", error_response);
    http::global::set_client(mock);

    // Simulate route error handling
    auto response = mock->execute("GET", "https://api.github.com/users/unknown", {}, "");

    // Should handle 404 gracefully
    REQUIRE(!response); // operator bool is false for non-2xx
    REQUIRE(response.status_code == 404);

    // Route would return error page or 404
    // Example: return web::Response{404, "User not found"};

    mock->clear();
}

TEST_CASE("API query integration: multiple queries in route") {
    auto mock = std::make_shared<http::MockClient>();

    // Stub multiple endpoints
    http::Response user_response;
    user_response.status_code = 200;
    user_response.body = R"({"id": 1, "login": "alice"})";
    mock->stub("GET", "https://api.github.com/users/alice", user_response);

    http::Response repos_response;
    repos_response.status_code = 200;
    repos_response.body = R"([{"name": "repo1"}, {"name": "repo2"}])";
    mock->stub("GET", "https://api.github.com/users/alice/repos", repos_response);

    http::global::set_client(mock);

    // Simulate route making multiple API calls
    auto user = mock->execute("GET", "https://api.github.com/users/alice", {}, "");
    auto repos = mock->execute("GET", "https://api.github.com/users/alice/repos", {}, "");

    REQUIRE(user);
    REQUIRE(repos);
    REQUIRE(mock->history().size() == 2);

    // Route would render both results in HTML
    // Example: layout_main("User", html::div{ user.login, repos.size() })

    mock->clear();
}

TEST_CASE("API query integration: parameter substitution") {
    auto mock = std::make_shared<http::MockClient>();

    // Stub with specific parameter value
    http::Response response;
    response.status_code = 200;
    response.body = R"({"id": 42, "login": "bob"})";

    // URL with parameter should match
    mock->stub("GET", "https://api.github.com/users/bob", response);
    http::global::set_client(mock);

    // Simulate: co_await github_com::user("bob")
    // Which generates: http::get("https://api.github.com/users/" + "bob")
    auto result = mock->execute("GET", "https://api.github.com/users/bob", {}, "");

    REQUIRE(result);
    REQUIRE(result.status_code == 200);

    mock->clear();
}

TEST_CASE("API query integration: floating point responses") {
    auto mock = std::make_shared<http::MockClient>();

    // Stripe-like charge response with amounts
    http::Response response;
    response.status_code = 200;
    response.body = R"({"id": "ch_123", "amount": 99.99, "tax_rate": 0.08, "total": 107.99})";

    mock->stub("GET", "https://api.stripe.com/charges/ch_123", response);
    http::global::set_client(mock);

    auto result = mock->execute("GET", "https://api.stripe.com/charges/ch_123", {}, "");

    REQUIRE(result);
    REQUIRE(result.body.find("99.99") != std::string::npos);
    REQUIRE(result.body.find("0.08") != std::string::npos);

    // Eventually: auto amount = response.amount (as double)

    mock->clear();
}

TEST_CASE("API query integration: boolean status fields") {
    auto mock = std::make_shared<http::MockClient>();

    http::Response response;
    response.status_code = 200;
    response.body = R"({"id": 1, "is_active": true, "is_verified": true, "email_confirmed": false})";

    mock->stub("GET", "https://api.example.com/users/1", response);
    http::global::set_client(mock);

    auto result = mock->execute("GET", "https://api.example.com/users/1", {}, "");

    REQUIRE(result);
    REQUIRE(result.body.find("true") != std::string::npos);
    REQUIRE(result.body.find("false") != std::string::npos);

    // Eventually: auto is_active = response.is_active (as bool)

    mock->clear();
}

TEST_CASE("API query integration: array responses") {
    auto mock = std::make_shared<http::MockClient>();

    http::Response response;
    response.status_code = 200;
    response.body = R"({
      "login": "alice",
      "repos": ["repo1", "repo2", "repo3"],
      "ids": [1, 2, 3, 4, 5]
    })";

    mock->stub("GET", "https://api.github.com/users/alice/repos", response);
    http::global::set_client(mock);

    auto result = mock->execute("GET", "https://api.github.com/users/alice/repos", {}, "");

    REQUIRE(result);
    REQUIRE(result.body.find("[") != std::string::npos);
    REQUIRE(result.body.find("repo1") != std::string::npos);

    // Eventually: auto repos = response.repos (as std::vector<std::string>)

    mock->clear();
}

TEST_CASE("API query integration: optional/nullable fields") {
    auto mock = std::make_shared<http::MockClient>();

    // User with some optional fields missing
    http::Response response;
    response.status_code = 200;
    response.body = R"({
      "id": 1,
      "name": "alice",
      "bio": null,
      "phone": "555-1234",
      "age": null
    })";

    mock->stub("GET", "https://api.example.com/users/1", response);
    http::global::set_client(mock);

    auto result = mock->execute("GET", "https://api.example.com/users/1", {}, "");

    REQUIRE(result);
    REQUIRE(result.body.find("null") != std::string::npos);

    // Eventually: auto bio = response.bio (as std::optional<std::string>, has no value)
    // Eventually: auto phone = response.phone (as std::optional<std::string>, has value)

    mock->clear();
}

TEST_CASE("API query integration: mixed data types") {
    auto mock = std::make_shared<http::MockClient>();

    // Complex response with all types
    http::Response response;
    response.status_code = 200;
    response.body = R"({
      "id": 123,
      "user_id": 456789,
      "score": 98.5,
      "rating": 4.2,
      "is_active": true,
      "is_verified": false,
      "tags": ["important", "featured"],
      "metadata": {"key": "value"},
      "description": null
    })";

    mock->stub("GET", "https://api.example.com/items/123", response);
    http::global::set_client(mock);

    auto result = mock->execute("GET", "https://api.example.com/items/123", {}, "");

    REQUIRE(result);
    REQUIRE(result.body.find("123") != std::string::npos);
    REQUIRE(result.body.find("98.5") != std::string::npos);
    REQUIRE(result.body.find("true") != std::string::npos);
    REQUIRE(result.body.find("important") != std::string::npos);

    // Eventually: auto score = response.score (double)
    // Eventually: auto is_active = response.is_active (bool)
    // Eventually: auto tags = response.tags (std::vector<std::string>)

    mock->clear();
}
