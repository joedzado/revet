// Magic-link JSON endpoints on SQLite.
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <mutex>
#include <revet/auth/magic_http.hpp>
#include <revet/auth/passkeys.hpp>
#include <revet/auth/tokens.hpp>
#include <revet/db/sqlite.hpp>
#include <revet/json.hpp>

namespace http = web::auth::magic::http;
namespace magic = web::auth::magic;
namespace tok = web::auth::tokens;
namespace auth = web::auth;
using W = db::sqlite::Handle<db::Write>;

namespace {

struct Fixture {
    std::string path;
    db::sqlite::Pool pool;
    std::mutex mu;
    std::vector<web::mail::Message> sent;
    static auto unique_path() -> std::string {
        static std::atomic<int> seq{0};
        const auto p = "/tmp/revet_auth_magichttp_" + std::to_string(seq++) + ".db";
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::remove((p + suffix).c_str());
        }
        return p;
    }
    Fixture() : path(unique_path()), pool(path, 4) {
        W w(pool);
        auth::create_schema(w);
        tok::create_schema(w);
        magic::create_schema(w);
        auto& c = magic::config();
        c.base_url = "https://app.example.com";
        c.path = "/auth/magic";
        c.max_per_address_per_hour = 5;
        c.ttl = std::chrono::seconds(900);
        c.send = [this](web::mail::Message m) {
            const std::scoped_lock lock(mu);
            sent.push_back(std::move(m));
        };
    }
    ~Fixture() {
        magic::config().send = nullptr;
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::remove((path + suffix).c_str());
        }
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    auto open() {
        return [this] { return W(pool); };
    }
    auto last_token() -> std::string {
        const auto& t = sent.back().text;
        const auto at = t.find("?token=") + 7;
        return t.substr(at, t.find_first_of("\r\n ", at) - at);
    }
};

auto post(const std::string& json, const std::string& bearer = "") -> web::Request {
    web::Request req;
    req.method = web::Method::POST;
    req.raw_body = json;
    if (!bearer.empty()) {
        req.headers.data["authorization"] = "Bearer " + bearer;
    }
    return req;
}

auto error_of(const web::Response& r) -> std::string {
    revet::json::Document doc(r.body);
    return doc.root().get("error", "");
}

} // namespace

TEST_CASE("magic http: requesting a link answers the same for everyone", "[auth][magic][http]") {
    Fixture fx;
    {
        W w(fx.pool);
        magic::request(w, "known@example.com", magic::Purpose::sign_in, {}, 1);
        magic::complete(w, fx.last_token(), 2); // known@example.com now has an account
    }
    fx.sent.clear();

    const auto known = http::request(post(R"({"email":"known@example.com"})"), fx.open());
    const auto unknown = http::request(post(R"({"email":"stranger@example.com"})"), fx.open());
    REQUIRE(known.status.code == 200);
    REQUIRE(known.body == R"({"ok":true})");
    REQUIRE(unknown.status.code == 200);
    REQUIRE(unknown.body == known.body);
    REQUIRE(fx.sent.size() == 2);

    // Past the hourly cap nothing is sent, but the answer does not change.
    for (int i = 0; i < 6; ++i) {
        http::request(post(R"({"email":"stranger@example.com"})"), fx.open());
    }
    const auto throttled = http::request(post(R"({"email":"stranger@example.com"})"), fx.open());
    REQUIRE(throttled.status.code == 200);
    REQUIRE(throttled.body == known.body);
    REQUIRE(fx.sent.size() == 2 + 4); // 5 in the hour for stranger, 1 for known
}

TEST_CASE("magic http: malformed requests are 400s", "[auth][magic][http]") {
    Fixture fx;
    REQUIRE(error_of(http::request(web::Request{}, fx.open())) == "json_body_required");
    REQUIRE(error_of(http::request(post("nope"), fx.open())) == "bad_json");
    REQUIRE(error_of(http::request(post("{}"), fx.open())) == "invalid_email");
    REQUIRE(error_of(http::request(post(R"({"email":"not an address"})"), fx.open())) == "invalid_email");
    REQUIRE(error_of(http::request(post(R"({"email":"a@b.com\r\nBcc: x@y.com"})"), fx.open())) == "invalid_email");
    REQUIRE(http::request(post(R"({"email":123})"), fx.open()).status.code == 400);
    REQUIRE(fx.sent.empty());
}

TEST_CASE("magic http: verifying a link returns a session", "[auth][magic][http]") {
    Fixture fx;
    http::request(post(R"({"email":"ann@example.com"})"), fx.open());
    const auto token = fx.last_token();

    const auto r = http::verify(post(R"({"token":")" + token + R"(","device":"Ann's phone"})"), fx.open());
    REQUIRE(r.status.code == 200);
    revet::json::Document doc(r.body);
    const std::string user = doc.root().at("user_id");
    REQUIRE(static_cast<bool>(doc.root().at("new_user")));
    const std::string access = doc.root().at("tokens").at("access_token");
    REQUIRE(tok::verify_access(access)->user_id == user);

    // The link is spent.
    const auto again = http::verify(post(R"({"token":")" + token + R"("})"), fx.open());
    REQUIRE(again.status.code == 401);
    REQUIRE(error_of(again) == "invalid_link");

    // The device label reached the session.
    W w(fx.pool);
    REQUIRE(tok::list_devices(w, user).at(0).label == "Ann's phone");
}

TEST_CASE("magic http: bad tokens are all the same 401", "[auth][magic][http]") {
    Fixture fx;
    for (const char* body : {R"({"token":"nope"})", R"({"token":""})", R"({"token":"' OR 1=1 --"})"}) {
        const auto r = http::verify(post(body), fx.open());
        REQUIRE(r.status.code == 401);
        REQUIRE(error_of(r) == "invalid_link");
    }
    REQUIRE(error_of(http::verify(web::Request{}, fx.open())) == "json_body_required");
    REQUIRE(error_of(http::verify(post("["), fx.open())) == "bad_json");
}

TEST_CASE("magic http: a signed-in user adds an address with a link", "[auth][magic][http]") {
    Fixture fx;
    std::string user, access;
    {
        W w(fx.pool);
        user = auth::find_or_create_user(w, "passkey", "cred");
        access = tok::issue(w, user, "phone", tok::detail::unix_now()).access_token;
    }
    REQUIRE(http::request(post(R"({"email":"ann@example.com"})", access), fx.open()).status.code == 200);
    REQUIRE(fx.sent.back().subject.find("Confirm") == 0);

    const auto r = http::verify(post(R"({"token":")" + fx.last_token() + R"("})"), fx.open());
    REQUIRE(r.status.code == 200);
    revet::json::Document doc(r.body);
    REQUIRE(static_cast<std::string>(doc.root().at("user_id")) == user);
    REQUIRE(static_cast<bool>(doc.root().at("linked")));
    REQUIRE_FALSE(doc.root().has("tokens")); // adding an address never hands out a session

    W w(fx.pool);
    REQUIRE(auth::find_user(w, "email", "ann@example.com") == user);
}

TEST_CASE("magic http: an unusable token on the request is refused, not ignored", "[auth][magic][http]") {
    Fixture fx;
    REQUIRE(http::request(post(R"({"email":"ann@example.com"})", "garbage"), fx.open()).status.code == 401);
    web::Request req = post(R"({"email":"ann@example.com"})");
    req.headers.data["authorization"] = "Basic abc";
    REQUIRE(http::request(req, fx.open()).status.code == 401);
    REQUIRE(fx.sent.empty());
}

TEST_CASE("magic http: adding an address that is someone else's is a 409", "[auth][magic][http]") {
    Fixture fx;
    http::request(post(R"({"email":"ann@example.com"})"), fx.open());
    REQUIRE(http::verify(post(R"({"token":")" + fx.last_token() + R"("})"), fx.open()).status.code == 200);

    std::string access;
    {
        W w(fx.pool);
        const auto other = auth::find_or_create_user(w, "passkey", "cred");
        access = tok::issue(w, other, "x", tok::detail::unix_now()).access_token;
    }
    http::request(post(R"({"email":"ann@example.com"})", access), fx.open());
    const auto r = http::verify(post(R"({"token":")" + fx.last_token() + R"("})"), fx.open());
    REQUIRE(r.status.code == 409);
    REQUIRE(error_of(r) == "email_taken");
}
