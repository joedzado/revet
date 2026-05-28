#include <catch2/catch_test_macros.hpp>
#include <revet/crypto.hpp>
#include <revet/csrf.hpp>
#include <revet/web.hpp>

// Helper: produce a sealed cookie value for a known raw token, same way the
// middleware does, so tests can simulate a client returning a server-issued
// cookie.
static std::string seal(const std::string& raw) {
    return web::crypto::encrypt(raw);
}

TEST_CASE("CSRF Token Generation", "[csrf]") {
    auto t1 = web::csrf::generate_token();
    auto t2 = web::csrf::generate_token();
    REQUIRE(t1.length() == 64); // 32 bytes hex
    REQUIRE(t1 != t2);
}

TEST_CASE("CSRF Middleware Logic", "[csrf]") {
    web::Handler handler = [](const web::Request&) { return web::to_response("OK"); };

    SECTION("GET generates token if missing") {
        web::Request req;
        req.method = web::Method::GET;
        auto resp = web::dispatch(req, handler, {web::csrf::middleware});
        REQUIRE(resp.cookies.data.contains("_csrf"));
        REQUIRE(!resp.cookies.data.at("_csrf").empty());
        // The set cookie must be sealed (not the raw 64-hex token), otherwise
        // a subdomain attacker could forge it.
        const auto& cookie = resp.cookies.data.at("_csrf");
        REQUIRE(cookie.length() > 64);
        REQUIRE(!web::crypto::decrypt(cookie).empty());
    }

    SECTION("POST fails without token in form/header") {
        web::Request req;
        req.method = web::Method::POST;
        req.cookies.data["_csrf"] = seal("token123");
        auto resp = web::dispatch(req, handler, {web::csrf::middleware});
        REQUIRE(resp.status == 403);
    }

    SECTION("POST fails with mismatched token") {
        web::Request req;
        req.method = web::Method::POST;
        req.cookies.data["_csrf"] = seal("token123");
        req.form.data["csrf_token"] = "wrong_token";
        auto resp = web::dispatch(req, handler, {web::csrf::middleware});
        REQUIRE(resp.status == 403);
    }

    SECTION("POST succeeds with valid token in form") {
        web::Request req;
        req.method = web::Method::POST;
        req.cookies.data["_csrf"] = seal("token123");
        req.form.data["csrf_token"] = "token123";
        auto resp = web::dispatch(req, handler, {web::csrf::middleware});
        REQUIRE(resp.status == 200);
        REQUIRE(resp.body == "OK");
    }

    SECTION("POST succeeds with valid token in header") {
        web::Request req;
        req.method = web::Method::POST;
        req.cookies.data["_csrf"] = seal("token123");
        req.headers.data["x-csrf-token"] = "token123";
        auto resp = web::dispatch(req, handler, {web::csrf::middleware});
        REQUIRE(resp.status == 200);
        REQUIRE(resp.body == "OK");
    }
}

TEST_CASE("CSRF blocks PATCH without token", "[csrf]") {
    web::Handler handler = [](const web::Request&) { return web::to_response("OK"); };

    SECTION("PATCH fails without token") {
        web::Request req;
        req.method = web::Method::PATCH;
        req.cookies.data["_csrf"] = seal("token123");
        auto resp = web::dispatch(req, handler, {web::csrf::middleware});
        REQUIRE(resp.status == 403);
    }

    SECTION("PATCH succeeds with valid token") {
        web::Request req;
        req.method = web::Method::PATCH;
        req.cookies.data["_csrf"] = seal("token123");
        req.form.data["csrf_token"] = "token123";
        auto resp = web::dispatch(req, handler, {web::csrf::middleware});
        REQUIRE(resp.status == 200);
    }
}

// ── Security regressions ────────────────────────────────────────────────────

TEST_CASE("CSRF rejects forged plaintext cookies (subdomain bypass)", "[csrf][security]") {
    // A subdomain attacker who can set cookies on the parent domain doesn't
    // hold REVET_COOKIE_KEY. They can set a plaintext "_csrf" cookie and a
    // matching form field, but the cookie won't decrypt — fail closed.
    web::Handler handler = [](const web::Request&) { return web::to_response("OK"); };

    web::Request req;
    req.method = web::Method::POST;
    req.cookies.data["_csrf"]   = "attacker-controlled-value";  // not AEAD
    req.form.data["csrf_token"] = "attacker-controlled-value";  // same string

    auto resp = web::dispatch(req, handler, {web::csrf::middleware});
    REQUIRE(resp.status == 403);
}

TEST_CASE("CSRF rejects tampered ciphertext", "[csrf][security]") {
    // Real sealed cookie, but flip one byte. AEAD verification fails.
    web::Handler handler = [](const web::Request&) { return web::to_response("OK"); };

    std::string sealed = seal("legit-token");
    sealed[sealed.size() / 2] = (sealed[sealed.size() / 2] == 'A') ? 'B' : 'A';

    web::Request req;
    req.method = web::Method::POST;
    req.cookies.data["_csrf"]   = sealed;
    req.form.data["csrf_token"] = "legit-token";
    auto resp = web::dispatch(req, handler, {web::csrf::middleware});
    REQUIRE(resp.status == 403);
}

TEST_CASE("CSRF: cross-user token replay rejected", "[csrf][security]") {
    // User A's sealed cookie + User A's raw token must NOT validate when
    // submitted as someone else's request whose cookie was minted with a
    // different raw token. Each token is single-cookie-bound.
    web::Handler handler = [](const web::Request&) { return web::to_response("OK"); };

    web::Request req;
    req.method = web::Method::POST;
    req.cookies.data["_csrf"]   = seal("user-A-token");
    req.form.data["csrf_token"] = "user-B-token";
    auto resp = web::dispatch(req, handler, {web::csrf::middleware});
    REQUIRE(resp.status == 403);
}

TEST_CASE("CSRF: fresh visitor POST is rejected", "[csrf][security]") {
    // First-ever request, no prior GET. Client has no cookie yet, but tries
    // to POST. Must 403 — they can't possibly know the server's not-yet-
    // -issued token.
    web::Handler handler = [](const web::Request&) { return web::to_response("OK"); };

    web::Request req;
    req.method = web::Method::POST;
    req.form.data["csrf_token"] = "guess";
    auto resp = web::dispatch(req, handler, {web::csrf::middleware});
    REQUIRE(resp.status == 403);
}

TEST_CASE("CSRF: GET refresh issues new cookie when prior cookie unsealable",
          "[csrf][security]") {
    // If the cookie is garbage, the middleware rotates a new sealed cookie.
    web::Handler handler = [](const web::Request&) { return web::to_response("OK"); };

    web::Request req;
    req.method = web::Method::GET;
    req.cookies.data["_csrf"] = "garbage";
    auto resp = web::dispatch(req, handler, {web::csrf::middleware});
    REQUIRE(resp.cookies.data.contains("_csrf"));
    REQUIRE(!web::crypto::decrypt(resp.cookies.data.at("_csrf")).empty());
}

TEST_CASE("CSRF Field Helper", "[csrf]") {
    web::Request req;
    web::Context::current().states[typeid(web::csrf::CsrfToken)] = web::csrf::CsrfToken{"my-token"};
    auto node = web::csrf::field(req);
    std::string html = node;
    REQUIRE(html.find("type=\"hidden\"") != std::string::npos);
    REQUIRE(html.find("name=\"csrf_token\"") != std::string::npos);
    REQUIRE(html.find("value=\"my-token\"") != std::string::npos);
}
