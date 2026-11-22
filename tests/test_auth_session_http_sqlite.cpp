// Refresh, logout, device list and account deletion endpoints on SQLite.
#include <atomic>
#include <cstdio>

#include <catch2/catch_test_macros.hpp>
#include <revet/auth/passkeys.hpp>
#include <revet/auth/session_http.hpp>
#include <revet/auth/tokens.hpp>
#include <revet/db/sqlite.hpp>
#include <revet/json.hpp>

namespace sess = web::auth::session::http;
namespace tok = web::auth::tokens;
namespace auth = web::auth;
using W = db::sqlite::Handle<db::Write>;

namespace {

struct Fixture {
    std::string path;
    db::sqlite::Pool pool;
    static auto unique_path() -> std::string {
        static std::atomic<int> seq{0};
        const auto p = "/tmp/revet_auth_session_" + std::to_string(seq++) + ".db";
        for (const char* suffix : {"", "-wal", "-shm"}) std::remove((p + suffix).c_str()); // left by a crashed run
        return p;
    }
    Fixture() : path(unique_path()), pool(path, 4) {
        W w(pool);
        auth::create_schema(w);
        tok::create_schema(w);
        auth::passkeys::create_schema(w);
    }
    ~Fixture() {
        std::remove(path.c_str());
        std::remove((path + "-wal").c_str());
        std::remove((path + "-shm").c_str());
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    auto open() { return [this] { return W(pool); }; }
};

auto post(const std::string& json) -> web::Request {
    web::Request req;
    req.method = web::Method::POST;
    req.raw_body = json;
    return req;
}

auto bearer(const std::string& token) -> web::Request {
    web::Request req;
    req.headers.data["authorization"] = "Bearer " + token;
    return req;
}

auto error_of(const web::Response& r) -> std::string {
    revet::json::Document doc(r.body);
    return doc.root().get("error", "");
}

auto refresh_body(const std::string& token) -> std::string {
    return R"({"refresh_token":")" + token + R"("})";
}

auto guarded(const web::Request& req, const std::function<web::Response(const web::Request&)>& h) -> web::Response {
    return web::dispatch(req, h, {tok::guard()});
}

} // namespace

TEST_CASE("session http: refresh trades a token for a new pair", "[auth][session]") {
    Fixture fx;
    std::string user;
    tok::TokenPair first;
    {
        W w(fx.pool);
        user = auth::find_or_create_user(w, "t", "alice");
        first = tok::issue(w, user, "phone");
    }

    const auto r = sess::refresh(post(refresh_body(first.refresh_token)), fx.open());
    REQUIRE(r.status.code == 200);
    REQUIRE(r.content_type == "application/json");
    revet::json::Document doc(r.body);
    const std::string access = doc.root().at("tokens").at("access_token");
    const std::string refresh = doc.root().at("tokens").at("refresh_token");
    REQUIRE(tok::verify_access(access)->user_id == user);
    REQUIRE(refresh != first.refresh_token);
    // And the new refresh token works in turn.
    REQUIRE(sess::refresh(post(refresh_body(refresh)), fx.open()).status.code == 200);
}

TEST_CASE("session http: refresh refusals tell the app to sign in again", "[auth][session]") {
    Fixture fx;
    tok::TokenPair first, expired_pair;
    {
        W w(fx.pool);
        const auto user = auth::find_or_create_user(w, "t", "alice");
        first = tok::issue(w, user, "phone");
        expired_pair = tok::issue(w, user, "old", 1000); // expired long ago
    }
    REQUIRE(sess::refresh(post(refresh_body(first.refresh_token)), fx.open()).status.code == 200);

    const auto reused = sess::refresh(post(refresh_body(first.refresh_token)), fx.open());
    REQUIRE(reused.status.code == 401);
    REQUIRE(error_of(reused) == "session_revoked");

    const auto expired = sess::refresh(post(refresh_body(expired_pair.refresh_token)), fx.open());
    REQUIRE(expired.status.code == 401);
    REQUIRE(error_of(expired) == "session_expired");

    const auto unknown = sess::refresh(post(refresh_body("garbage")), fx.open());
    REQUIRE(unknown.status.code == 401);
    REQUIRE(error_of(unknown) == "invalid_refresh_token");
}

TEST_CASE("session http: refresh rejects malformed requests", "[auth][session]") {
    Fixture fx;
    REQUIRE(error_of(sess::refresh(web::Request{}, fx.open())) == "json_body_required");
    REQUIRE(error_of(sess::refresh(post("nope"), fx.open())) == "bad_json");
    REQUIRE(error_of(sess::refresh(post("{}"), fx.open())) == "bad_request");
    REQUIRE(error_of(sess::refresh(post(R"({"refresh_token":""})"), fx.open())) == "bad_request");
}

TEST_CASE("session http: logout ends this device only", "[auth][session]") {
    Fixture fx;
    std::string user;
    tok::TokenPair phone, laptop;
    {
        W w(fx.pool);
        user = auth::find_or_create_user(w, "t", "alice");
        phone = tok::issue(w, user, "phone", tok::detail::unix_now());
        laptop = tok::issue(w, user, "laptop", tok::detail::unix_now());
    }
    const auto out = guarded(bearer(phone.access_token), [&](const web::Request& r) { return sess::logout(r, fx.open()); });
    REQUIRE(out.status.code == 200);
    REQUIRE(out.body == R"({"ok":true})");

    REQUIRE(error_of(sess::refresh(post(refresh_body(phone.refresh_token)), fx.open())) == "session_revoked");
    REQUIRE(sess::refresh(post(refresh_body(laptop.refresh_token)), fx.open()).status.code == 200);
}

TEST_CASE("session http: logout needs a token", "[auth][session]") {
    Fixture fx;
    REQUIRE(guarded(web::Request{}, [&](const web::Request& r) { return sess::logout(r, fx.open()); }).status.code == 401);
}

TEST_CASE("session http: logout everywhere and the device list", "[auth][session]") {
    Fixture fx;
    std::string user;
    tok::TokenPair phone, laptop;
    {
        W w(fx.pool);
        user = auth::find_or_create_user(w, "t", "alice");
        phone = tok::issue(w, user, "phone", tok::detail::unix_now());
        laptop = tok::issue(w, user, "laptop", tok::detail::unix_now());
    }
    const auto list = guarded(bearer(phone.access_token), [&](const web::Request& r) { return sess::devices(r, fx.open()); });
    REQUIRE(list.status.code == 200);
    revet::json::Document doc(list.body);
    int current = 0;
    for (std::size_t i = 0; i < 2; ++i) {
        if (static_cast<bool>(doc.root().at("devices").at(i).at("current"))) {
            ++current;
            REQUIRE(static_cast<std::string>(doc.root().at("devices").at(i).at("session_id")) == phone.session_id);
        }
    }
    REQUIRE(current == 1);

    const auto all = guarded(bearer(phone.access_token), [&](const web::Request& r) { return sess::logout_all(r, fx.open()); });
    REQUIRE(all.status.code == 200);
    revet::json::Document ad(all.body);
    REQUIRE(static_cast<int>(ad.root().at("signed_out")) == 2);
    REQUIRE(error_of(sess::refresh(post(refresh_body(laptop.refresh_token)), fx.open())) == "session_revoked");
}

TEST_CASE("session http: deleting an account removes everything", "[auth][session]") {
    Fixture fx;
    std::string user, other;
    tok::TokenPair pair;
    {
        W w(fx.pool);
        user = auth::find_or_create_user(w, "t", "alice");
        other = auth::find_or_create_user(w, "t", "bob");
        auth::link_identity(w, user, "google", "g-1");
        pair = tok::issue(w, user, "phone", tok::detail::unix_now());
        tok::issue(w, other, "phone");
    }
    const auto gone = guarded(bearer(pair.access_token), [&](const web::Request& r) { return sess::delete_account(r, fx.open()); });
    REQUIRE(gone.status.code == 200);

    {
        W w(fx.pool);
        REQUIRE_FALSE(auth::user_exists(w, user));
        REQUIRE_FALSE(auth::find_user(w, "google", "g-1").has_value());
        REQUIRE(w.scalar<long long>("SELECT COUNT(*) FROM auth_sessions WHERE user_id = ?", user).value_or(-1) == 0);
        REQUIRE(auth::user_exists(w, other));
        REQUIRE(w.scalar<long long>("SELECT COUNT(*) FROM auth_sessions").value_or(-1) == 1);
    }

    // The deleted user's access token still verifies (it is sealed, not looked up) but their refresh token is dead,
    // and a second delete finds nothing.
    REQUIRE(error_of(sess::refresh(post(refresh_body(pair.refresh_token)), fx.open())) == "invalid_refresh_token");
    const auto again = guarded(bearer(pair.access_token), [&](const web::Request& r) { return sess::delete_account(r, fx.open()); });
    REQUIRE(again.status.code == 404);
}
