// The shared auth rate limits: per address, per named credential or session, per signed-in user.
#include <catch2/catch_test_macros.hpp>
#include <revet/auth/limits.hpp>

namespace lim = web::auth::limits;
namespace tok = web::auth::tokens;

namespace {

// Small buckets so the tests are short. Must be set before the limiters are first built.
const bool configured = [] {
    lim::config().per_ip = {.burst = 3, .per_second = 0.001, .max_keys = 1000};
    lim::config().per_name = {.burst = 2, .per_second = 0.001, .max_keys = 1000};
    lim::config().per_user = {.burst = 4, .per_second = 0.001, .max_keys = 1000};
    lim::config().per_email = {.burst = 2, .per_second = 0.001, .max_keys = 1000};
    lim::config().all_mail = {.burst = 6, .per_second = 0.001, .max_keys = 1};
    return true;
}();

auto call(const std::vector<web::Middleware>& guards, const web::Request& req) -> web::Response {
    return web::dispatch(req, [](const web::Request&) { return web::Response{.body = "ok"}; }, guards);
}

auto from(const std::string& ip, const std::string& body = "") -> web::Request {
    web::Request req;
    req.peer_ip = ip;
    req.raw_body = body;
    req.method = web::Method::POST;
    return req;
}

auto with_token(const std::string& user, const std::string& ip = "10.9.9.9") -> web::Request {
    web::Request req;
    req.peer_ip = ip;
    const auto exp = tok::detail::unix_now() + 600;
    req.headers.data["authorization"] = "Bearer " + tok::detail::make_access_token({user, std::string(32, 'a')}, exp);
    return req;
}

} // namespace

TEST_CASE("auth limits: anonymous endpoints are limited per client address", "[auth][limits]") {
    REQUIRE(configured);
    const auto guards = lim::anonymous();
    for (int i = 0; i < 3; ++i) {
        REQUIRE(call(guards, from("198.51.100.1")).body == "ok");
    }
    const auto refused = call(guards, from("198.51.100.1"));
    REQUIRE(refused.status.code == 429);
    REQUIRE_FALSE(refused.headers.get("Retry-After").empty());

    REQUIRE(call(guards, from("198.51.100.2")).body == "ok"); // someone else is unaffected
}

TEST_CASE("auth limits: a credential is limited across addresses", "[auth][limits]") {
    const auto guards = lim::anonymous();
    const std::string body = R"({"credential_id":"abc123","signature":"x"})";
    // Three different addresses, one credential: the attacker's rotating IPs do not help.
    REQUIRE(call(guards, from("203.0.113.1", body)).body == "ok");
    REQUIRE(call(guards, from("203.0.113.2", body)).body == "ok");
    REQUIRE(call(guards, from("203.0.113.3", body)).status.code == 429);
    // Other credentials from the same fresh address are fine.
    REQUIRE(call(guards, from("203.0.113.4", R"({"credential_id":"other"})")).body == "ok");
}

TEST_CASE("auth limits: a session's refresh tokens are limited by session id", "[auth][limits]") {
    const auto guards = lim::anonymous();
    const std::string sid(32, 'c');
    const auto body = [&](const std::string& secret) {
        return R"({"refresh_token":")" + sid + ".7." + secret + R"("})";
    };
    REQUIRE(call(guards, from("192.0.2.1", body("a"))).body == "ok");
    REQUIRE(call(guards, from("192.0.2.2", body("b"))).body == "ok");
    REQUIRE(call(guards, from("192.0.2.3", body("c"))).status.code == 429);
}

TEST_CASE("auth limits: bodies that name nothing fall back to the address limit alone", "[auth][limits]") {
    const auto guards = lim::anonymous();
    for (const char* body : {"", "not json", "[]", R"({"credential_id":""})", R"({"x":1})"}) {
        // Each uses its own address, so only the per-name limit could stop it: it must not.
        static int n = 0;
        const auto ip = "192.0.2." + std::to_string(100 + n++);
        REQUIRE(call(guards, from(ip, body)).body == "ok");
        REQUIRE(call(guards, from(ip, body)).body == "ok");
    }
}

TEST_CASE("auth limits: signed-in endpoints need a token and then limit per user", "[auth][limits]") {
    const auto guards = lim::signed_in();
    REQUIRE(call(guards, from("10.1.1.1")).status.code == 401);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(call(guards, with_token("alice")).body == "ok");
    }
    REQUIRE(call(guards, with_token("alice")).status.code == 429);
    // The limit follows the user, not the address.
    REQUIRE(call(guards, with_token("alice", "10.8.8.8")).status.code == 429);
    REQUIRE(call(guards, with_token("bob")).body == "ok");
}

TEST_CASE("auth limits: refused unauthenticated requests do not spend a user's allowance", "[auth][limits]") {
    const auto guards = lim::signed_in();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(call(guards, from("10.2.2.2")).status.code == 401);
    }
    for (int i = 0; i < 4; ++i) {
        REQUIRE(call(guards, with_token("carol")).body == "ok");
    }
}

namespace {
auto mail_request(const std::string& ip, const std::string& email) -> web::Request {
    return from(ip, R"({"email":")" + email + R"("})");
}
} // namespace

TEST_CASE("auth limits: mail to one address is limited across clients", "[auth][limits][mail]") {
    lim::detail::reset(); // Catch2 runs cases in random order; the global mail cap is shared
    const auto guards = lim::magic_request();
    REQUIRE(call(guards, mail_request("198.18.0.1", "victim@example.com")).body == "ok");
    REQUIRE(call(guards, mail_request("198.18.0.2", "victim@example.com")).body == "ok");
    const auto refused = call(guards, mail_request("198.18.0.3", "victim@example.com"));
    REQUIRE(refused.status.code == 429);
    // Case and spaces do not make a different address.
    REQUIRE(call(guards, mail_request("198.18.0.4", " Victim@Example.COM ")).status.code == 429);
    // Another address is fine.
    REQUIRE(call(guards, mail_request("198.18.0.5", "other@example.com")).body == "ok");
}

TEST_CASE("auth limits: a form field works as well as JSON", "[auth][limits][mail]") {
    lim::detail::reset();
    const auto guards = lim::magic_request();
    web::Request req;
    req.peer_ip = "198.18.1.1";
    req.form.data["email"] = "formuser@example.com";
    REQUIRE(call(guards, req).body == "ok");
    req.peer_ip = "198.18.1.2";
    REQUIRE(call(guards, req).body == "ok");
    req.peer_ip = "198.18.1.3";
    REQUIRE(call(guards, req).status.code == 429);
}

TEST_CASE("auth limits: all the mail the app sends has a ceiling", "[auth][limits][mail]") {
    lim::detail::reset();
    const auto guards = lim::magic_request();
    // Distinct addresses from distinct clients, so only the global cap of 6 can stop them.
    int allowed = 0;
    int ip = 0;
    for (int i = 0; i < 20; ++i) {
        const auto r =
            call(guards, mail_request("198.19.0." + std::to_string(++ip), "user" + std::to_string(i) + "@example.com"));
        if (r.body == "ok") {
            ++allowed;
        } else {
            REQUIRE(r.status.code == 429);
        }
    }
    REQUIRE(allowed == 6);
    REQUIRE(call(guards, mail_request("198.19.9.9", "late@example.com")).status.code == 429);
}

TEST_CASE("auth limits: a request that names no valid address is only limited by client", "[auth][limits][mail]") {
    // email_key is empty, so the recipient limit does not apply; the handler rejects the request anyway.
    REQUIRE(lim::detail::email_key(from("1.1.1.1", R"({"email":"not an address"})")).empty());
    REQUIRE(lim::detail::email_key(from("1.1.1.1", "")).empty());
    REQUIRE(lim::detail::email_key(from("1.1.1.1", "[]")).empty());
    REQUIRE(lim::detail::email_key(from("1.1.1.1", R"({"email":"A@B.CO"})")) == "email:a@b.co");
}
