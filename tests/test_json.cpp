#include <catch2/catch_test_macros.hpp>
#include <revet/json.hpp>

using namespace revet::json;

TEST_CASE("JSON wrapper: basic field access") {
    std::string json = R"({"name": "alice", "id": 123})";
    Document doc(json);

    auto name = static_cast<std::string>(doc.root().at("name"));
    auto id = static_cast<int32_t>(doc.root().at("id"));

    REQUIRE(name == "alice");
    REQUIRE(id == 123);
}

TEST_CASE("JSON wrapper: type conversions") {
    std::string json = R"({
      "count": 42,
      "price": 19.99,
      "is_active": true,
      "name": "item"
    })";
    Document doc(json);

    auto count = static_cast<int32_t>(doc.root().at("count"));
    auto price = static_cast<double>(doc.root().at("price"));
    auto is_active = static_cast<bool>(doc.root().at("is_active"));
    auto name = static_cast<std::string>(doc.root().at("name"));

    REQUIRE(count == 42);
    REQUIRE(price == 19.99);
    REQUIRE(is_active == true);
    REQUIRE(name == "item");
}

TEST_CASE("JSON wrapper: floating point types") {
    std::string json = R"({
      "float_val": 3.14,
      "double_val": 2.71828
    })";
    Document doc(json);

    auto f = static_cast<float>(doc.root().at("float_val"));
    auto d = static_cast<double>(doc.root().at("double_val"));

    REQUIRE((f > 3.1F && f < 3.2F));
    REQUIRE((d > 2.7 && d < 2.8));
}

TEST_CASE("JSON wrapper: unsigned integers") {
    std::string json = R"({
      "u32": 4294967295,
      "u64": 18446744073709551615
    })";
    Document doc(json);

    auto u32 = static_cast<uint32_t>(doc.root().at("u32"));
    auto u64 = static_cast<uint64_t>(doc.root().at("u64"));

    REQUIRE(u32 > 0);
    REQUIRE(u64 > 0);
}

TEST_CASE("JSON wrapper: nested objects") {
    std::string json = R"({
      "user": {
        "name": "alice",
        "profile": {
          "age": 30,
          "city": "NYC"
        }
      }
    })";
    Document doc(json);

    auto name = static_cast<std::string>(doc.root().at("user").at("name"));
    auto age = static_cast<int32_t>(doc.root().at("user").at("profile").at("age"));
    auto city = static_cast<std::string>(doc.root().at("user").at("profile").at("city"));

    REQUIRE(name == "alice");
    REQUIRE(age == 30);
    REQUIRE(city == "NYC");
}

TEST_CASE("JSON wrapper: arrays") {
    std::string json = R"({
      "tags": ["a", "b", "c"],
      "numbers": [1, 2, 3, 4, 5]
    })";
    Document doc(json);

    auto tags = static_cast<std::vector<std::string>>(doc.root().at("tags"));
    auto numbers = static_cast<std::vector<int32_t>>(doc.root().at("numbers"));

    REQUIRE(tags.size() == 3);
    REQUIRE(tags[0] == "a");
    REQUIRE(tags[1] == "b");
    REQUIRE(numbers.size() == 5);
    REQUIRE(numbers[2] == 3);
}

TEST_CASE("JSON wrapper: optional fields") {
    std::string json = R"({
      "id": 123,
      "name": "alice",
      "bio": null,
      "phone": "555-1234"
    })";
    Document doc(json);

    // bio is null - check explicitly
    REQUIRE(doc.root().at("bio").is_null());

    // phone is not null and is a string
    REQUIRE(!doc.root().at("phone").is_null());
    auto phone = static_cast<std::string>(doc.root().at("phone"));
    REQUIRE(phone == "555-1234");
}

TEST_CASE("JSON wrapper: missing fields throw") {
    std::string json = R"({"name": "alice"})";
    Document doc(json);

    REQUIRE_THROWS_AS(static_cast<std::string>(doc.root().at("missing")), ParseError);
}

TEST_CASE("JSON wrapper: type mismatch throws") {
    std::string json = R"({"value": "not_a_number"})";
    Document doc(json);

    REQUIRE_THROWS_AS(static_cast<int32_t>(doc.root().at("value")), ParseError);
}

TEST_CASE("JSON wrapper: has() method") {
    std::string json = R"({"name": "alice", "age": 30})";
    Document doc(json);

    REQUIRE(doc.root().has("name"));
    REQUIRE(doc.root().has("age"));
    REQUIRE(!doc.root().has("missing"));
}

TEST_CASE("JSON wrapper: null checking") {
    std::string json = R"({
      "value": null,
      "name": "alice"
    })";
    Document doc(json);

    REQUIRE(doc.root().at("value").is_null());
    REQUIRE(!doc.root().at("name").is_null());
}

TEST_CASE("JSON wrapper: complex realistic response") {
    std::string json = R"({
      "id": 12345,
      "user": {
        "login": "alice",
        "name": "Alice Smith",
        "email": "alice@example.com"
      },
      "repos": ["repo1", "repo2", "repo3"],
      "stats": {
        "followers": 1000,
        "rating": 4.8,
        "is_verified": true
      },
      "bio": null
    })";
    Document doc(json);

    auto id = static_cast<int32_t>(doc.root().at("id"));
    auto login = static_cast<std::string>(doc.root().at("user").at("login"));
    auto repos = static_cast<std::vector<std::string>>(doc.root().at("repos"));
    auto followers = static_cast<int32_t>(doc.root().at("stats").at("followers"));
    auto rating = static_cast<double>(doc.root().at("stats").at("rating"));
    auto is_verified = static_cast<bool>(doc.root().at("stats").at("is_verified"));

    REQUIRE(id == 12345);
    REQUIRE(login == "alice");
    REQUIRE(repos.size() == 3);
    REQUIRE(followers == 1000);
    REQUIRE((rating > 4.7 && rating < 4.9));
    REQUIRE(is_verified == true);
    REQUIRE(doc.root().at("bio").is_null());
}
