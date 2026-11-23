// The passkey JSON endpoints, end to end on SQLite: a software authenticator on one side, the
// handlers on the other, bytes over JSON in between.
#include <atomic>
#include <cstdio>

#include <catch2/catch_test_macros.hpp>
#include <revet/auth/passkeys.hpp>
#include <revet/auth/passkeys_http.hpp>
#include <revet/auth/tokens.hpp>
#include <revet/db/sqlite.hpp>
#include <revet/json.hpp>

#include "webauthn_soft_authenticator.hpp"

namespace http = web::auth::passkeys::http;
namespace tok = web::auth::tokens;
namespace wa = web::webauthn;
using W = db::sqlite::Handle<db::Write>;

namespace {

struct Fixture {
    std::string path;
    db::sqlite::Pool pool;
    static auto unique_path() -> std::string {
        static std::atomic<int> seq{0};
        const auto p = "/tmp/revet_auth_http_" + std::to_string(seq++) + ".db";
        for (const char* suffix : {"", "-wal", "-shm"}) std::remove((p + suffix).c_str()); // left by a crashed run
        return p;
    }
    Fixture() : path(unique_path()), pool(path, 4) {
        W w(pool);
        web::auth::create_schema(w);
        web::auth::tokens::create_schema(w);
        web::auth::passkeys::create_schema(w);
        auto& c = web::auth::passkeys::config();
        c.rp_name = "Example App";
        c.rp = {.rp_id = "app.example.com", .origins = {"https://app.example.com"}, .require_user_verification = true};
        c.apple_app_ids = {};
        c.android_apps = {};
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

auto post(const std::string& json, const std::string& bearer = "") -> web::Request {
    web::Request req;
    req.method = web::Method::POST;
    req.raw_body = json;
    req.headers.data["content-type"] = "application/json";
    if (!bearer.empty()) req.headers.data["authorization"] = "Bearer " + bearer;
    return req;
}

auto b64(const std::string& s) -> std::string {
    return wa::base64url_encode(s);
}

struct Parsed {
    revet::json::Document doc;
    explicit Parsed(const std::string& s) : doc(s) {}
    auto str(std::string_view k) -> std::string { return doc.root().get(k, ""); }
};

auto error_of(const web::Response& r) -> std::string {
    Parsed p(r.body);
    return p.str("error");
}

// Drives register_options + register_finish like the app would.
struct Registered {
    web::Response finish;
    std::string body;
};
auto register_via_http(Fixture& fx, soft::Authenticator& a, const std::string& bearer = "", const std::string& origin = "") -> web::Response {
    const auto opts = http::register_options(post(R"({"display_name":"Alex"})", bearer), fx.open());
    REQUIRE(opts.status.code == 200);
    Parsed o(opts.body);
    revet::json::Document doc(opts.body);
    const auto raw = *wa::base64url_decode(static_cast<std::string>(doc.at("options").at("challenge")));
    const auto r = a.registration(raw, {.origin = origin});
    const auto body = R"({"challenge_id":")" + o.str("challenge_id") + R"(","client_data_json":")" + b64(r.client_data_json) +
                      R"(","attestation_object":")" + b64(r.attestation_object) + R"(","label":"iPhone","device":"Alex's iPhone"})";
    return http::register_finish(post(body, ""), fx.open());
}

auto login_via_http(Fixture& fx, soft::Authenticator& a, const soft::Tweaks& t = {}) -> web::Response {
    const auto opts = http::login_options(post("{}"), fx.open());
    REQUIRE(opts.status.code == 200);
    Parsed o(opts.body);
    revet::json::Document doc(opts.body);
    const auto raw = *wa::base64url_decode(static_cast<std::string>(doc.at("options").at("challenge")));
    const auto r = a.assertion(raw, t);
    const auto body = R"({"challenge_id":")" + o.str("challenge_id") + R"(","credential_id":")" + b64(a.credential_id) +
                      R"(","client_data_json":")" + b64(r.client_data_json) + R"(","authenticator_data":")" + b64(r.authenticator_data) +
                      R"(","signature":")" + b64(r.signature) + R"(","device":"Alex's iPhone"})";
    return http::login_finish(post(body), fx.open());
}

auto access_token(const web::Response& r) -> std::string {
    revet::json::Document doc(r.body);
    return static_cast<std::string>(doc.at("tokens").at("access_token"));
}

auto guarded(const web::Middleware& guard, const web::Request& req, const std::function<web::Response(const web::Request&)>& h) -> web::Response {
    return web::dispatch(req, h, {guard});
}

} // namespace

TEST_CASE("passkey http: register then sign in, with a working session", "[auth][http]") {
    Fixture fx;
    soft::Authenticator a;

    const auto reg = register_via_http(fx, a);
    REQUIRE(reg.status.code == 200);
    REQUIRE(reg.content_type == "application/json");
    Parsed rp(reg.body);
    const auto user = rp.str("user_id");
    REQUIRE(user.size() == 32);
    REQUIRE(rp.str("credential_id") == b64(a.credential_id));
    revet::json::Document rdoc(reg.body);
    REQUIRE(static_cast<bool>(rdoc.at("new_user")));
    // A new account comes back signed in.
    REQUIRE(tok::verify_access(access_token(reg))->user_id == user);

    const auto login = login_via_http(fx, a);
    REQUIRE(login.status.code == 200);
    Parsed lp(login.body);
    REQUIRE(lp.str("user_id") == user);
    REQUIRE(tok::verify_access(access_token(login))->user_id == user);

    // The device label made it onto the session.
    W w(fx.pool);
    REQUIRE(tok::list_devices(w, user).size() == 2);
}

TEST_CASE("passkey http: options responses are the platform's JSON form", "[auth][http]") {
    Fixture fx;
    const auto reg = http::register_options(post("{}"), fx.open());
    REQUIRE(reg.status.code == 200);
    revet::json::Document doc(reg.body);
    REQUIRE(static_cast<std::string>(doc.at("options").at("rp").at("id")) == "app.example.com");
    REQUIRE(static_cast<std::string>(doc.at("options").at("user").at("name")) == "user"); // no display name sent

    const auto login = http::login_options(post("{}"), fx.open());
    revet::json::Document ld(login.body);
    REQUIRE(static_cast<std::string>(ld.at("options").at("rpId")) == "app.example.com");
}

TEST_CASE("passkey http: a signed-in user adds a passkey and does not get new tokens", "[auth][http]") {
    Fixture fx;
    soft::Authenticator phone;
    const auto first = register_via_http(fx, phone);
    const auto token = access_token(first);
    Parsed fp(first.body);

    soft::Authenticator laptop;
    laptop.credential_id = std::string(32, '\x22');
    const auto second = register_via_http(fx, laptop, token);
    REQUIRE(second.status.code == 200);
    Parsed sp(second.body);
    REQUIRE(sp.str("user_id") == fp.str("user_id"));
    revet::json::Document sd(second.body);
    REQUIRE_FALSE(static_cast<bool>(sd.at("new_user")));
    REQUIRE_FALSE(sd.root().has("tokens"));

    W w(fx.pool);
    REQUIRE(web::auth::passkeys::list_passkeys(w, fp.str("user_id")).size() == 2);
}

TEST_CASE("passkey http: a bad bearer token on register_options is refused, not ignored", "[auth][http]") {
    Fixture fx;
    REQUIRE(http::register_options(post("{}", "garbage"), fx.open()).status.code == 401);
    web::Request req = post("{}");
    req.headers.data["authorization"] = "Basic abc";
    REQUIRE(http::register_options(req, fx.open()).status.code == 401);
    W w(fx.pool);
    REQUIRE(w.scalar<long long>("SELECT COUNT(*) FROM auth_challenges").value_or(-1) == 0);
}

TEST_CASE("passkey http: a signed-out session cannot add a passkey with its unexpired access token", "[auth][http]") {
    Fixture fx;
    soft::Authenticator phone;
    const auto first = register_via_http(fx, phone);
    const auto token = access_token(first);
    revet::json::Document fd(first.body);
    const auto user = static_cast<std::string>(fd.at("user_id"));
    const auto session = static_cast<std::string>(fd.at("tokens").at("session_id"));
    {
        W w(fx.pool);
        REQUIRE(tok::revoke_session(w, user, session));
    }
    REQUIRE(tok::verify_access(token).has_value()); // the token alone still looks fine...
    const auto opts = http::register_options(post("{}", token), fx.open());
    REQUIRE(opts.status.code == 401); // ...but the session behind it is gone
    W w(fx.pool);
    REQUIRE(w.scalar<long long>("SELECT COUNT(*) FROM auth_challenges").value_or(-1) == 0);
}

TEST_CASE("passkey http: an oversized device name is cut to 128 bytes", "[auth][http]") {
    Fixture fx;
    soft::Authenticator a;
    const auto opts = http::register_options(post("{}"), fx.open());
    Parsed o(opts.body);
    revet::json::Document doc(opts.body);
    const auto raw = *wa::base64url_decode(static_cast<std::string>(doc.at("options").at("challenge")));
    const auto r = a.registration(raw, {});
    const auto body = R"({"challenge_id":")" + o.str("challenge_id") + R"(","client_data_json":")" + b64(r.client_data_json) +
                      R"(","attestation_object":")" + b64(r.attestation_object) + R"(","device":")" + std::string(5000, 'd') + R"("})";
    REQUIRE(http::register_finish(post(body), fx.open()).status.code == 200);
    W w(fx.pool);
    REQUIRE(w.scalar<long long>("SELECT MAX(LENGTH(label)) FROM auth_sessions").value_or(-1) == 128);
}

TEST_CASE("passkey http: registration says why it failed", "[auth][http]") {
    Fixture fx;
    soft::Authenticator a;
    const auto bad = register_via_http(fx, a, "", "https://evil.example.com");
    REQUIRE(bad.status.code == 400);
    Parsed p(bad.body);
    REQUIRE(p.str("error") == "verification_failed");
    REQUIRE(p.str("reason") == "wrong_origin");

    REQUIRE(register_via_http(fx, a).status.code == 200);
    soft::Authenticator again;
    again.credential_id = a.credential_id;
    const auto dup = register_via_http(fx, again);
    REQUIRE(dup.status.code == 409);
    REQUIRE(error_of(dup) == "credential_taken");
}

TEST_CASE("passkey http: login failures all look the same", "[auth][http]") {
    Fixture fx;
    soft::Authenticator a;
    REQUIRE(register_via_http(fx, a).status.code == 200);

    soft::Authenticator stranger;
    stranger.credential_id = std::string(32, 's');
    const auto unknown = login_via_http(fx, stranger);

    soft::Authenticator impostor;
    impostor.credential_id = a.credential_id;
    const auto forged = login_via_http(fx, impostor);

    const auto wrong_origin = login_via_http(fx, a, {.origin = "https://evil.example.com"});

    for (const auto* r : {&unknown, &forged, &wrong_origin}) {
        REQUIRE(r->status.code == 401);
        REQUIRE(r->body == R"({"error":"login_failed"})");
    }
    // Real sign-in still works afterwards.
    REQUIRE(login_via_http(fx, a).status.code == 200);
}

TEST_CASE("passkey http: malformed requests are 400s", "[auth][http]") {
    Fixture fx;
    web::Request empty;
    REQUIRE(http::register_finish(empty, fx.open()).status.code == 400);
    REQUIRE(error_of(http::login_finish(empty, fx.open())) == "json_body_required");
    REQUIRE(error_of(http::register_finish(post("not json"), fx.open())) == "bad_json");
    REQUIRE(error_of(http::register_finish(post("{}"), fx.open())) == "bad_request");
    REQUIRE(error_of(http::login_finish(post(R"({"challenge_id":"x"})"), fx.open())) == "bad_request");
    REQUIRE(error_of(http::login_finish(post(R"({"challenge_id":"nope","credential_id":"c","client_data_json":"AAAA","authenticator_data":"AAAA","signature":"AAAA"})"),
                                        fx.open())) == "challenge_invalid");
}

TEST_CASE("passkey http: list and remove sit behind the token guard", "[auth][http]") {
    Fixture fx;
    soft::Authenticator phone;
    const auto reg = register_via_http(fx, phone);
    const auto token = access_token(reg);
    Parsed rp(reg.body);

    const auto guard = tok::guard();
    web::Request get;
    get.headers.data["authorization"] = "Bearer " + token;
    const auto listed = guarded(guard, get, [&](const web::Request& r) { return http::list(r, fx.open()); });
    REQUIRE(listed.status.code == 200);
    revet::json::Document ld(listed.body);
    REQUIRE(static_cast<std::string>(ld.at("passkeys").at(0).at("credential_id")) == b64(phone.credential_id));
    REQUIRE(static_cast<std::string>(ld.at("passkeys").at(0).at("label")) == "iPhone");

    REQUIRE(guarded(guard, web::Request{}, [&](const web::Request& r) { return http::list(r, fx.open()); }).status.code == 401);

    // The only login method cannot be removed.
    auto rm = post(R"({"credential_id":")" + b64(phone.credential_id) + R"("})", token);
    const auto refused = guarded(guard, rm, [&](const web::Request& r) { return http::remove(r, fx.open()); });
    REQUIRE(refused.status.code == 409);
    REQUIRE(error_of(refused) == "last_login_method");

    auto missing = post(R"({"credential_id":"nope"})", token);
    REQUIRE(guarded(guard, missing, [&](const web::Request& r) { return http::remove(r, fx.open()); }).status.code == 404);

    // With a second passkey the first can go.
    soft::Authenticator laptop;
    laptop.credential_id = std::string(32, '\x22');
    REQUIRE(register_via_http(fx, laptop, token).status.code == 200);
    const auto removed = guarded(guard, rm, [&](const web::Request& r) { return http::remove(r, fx.open()); });
    REQUIRE(removed.status.code == 200);
    REQUIRE(removed.body == R"({"removed":true})");
}

TEST_CASE("passkey http: app association files", "[auth][http]") {
    Fixture fx;
    web::Request req;
    REQUIRE(http::apple_app_site_association(req).status.code == 404);
    REQUIRE(http::asset_links(req).status.code == 404);

    auto& c = web::auth::passkeys::config();
    c.apple_app_ids = {"ABCDE12345.com.example.app"};
    c.android_apps = {{.package_name = "com.example.app", .sha256_cert_fingerprints = {"AA:BB:CC", "DD:EE:FF"}}};

    const auto apple = http::apple_app_site_association(req);
    REQUIRE(apple.status.code == 200);
    REQUIRE(apple.content_type == "application/json");
    REQUIRE(apple.body == R"({"webcredentials":{"apps":["ABCDE12345.com.example.app"]},"applinks":{"details":[{"appIDs":["ABCDE12345.com.example.app"],"components":[{"/":"/auth/magic"}]}]}})");
    revet::json::Document apple_doc(apple.body);
    REQUIRE(static_cast<std::string>(apple_doc.root().at("applinks").at("details").at(0).at("components").at(0).at("/")) == "/auth/magic");

    const auto android = http::asset_links(req);
    REQUIRE(android.status.code == 200);
    revet::json::Document doc(android.body);
    REQUIRE(static_cast<std::string>(doc.root().at(0).at("relation").at(0)) == "delegate_permission/common.get_login_creds");
    REQUIRE(static_cast<std::string>(doc.root().at(0).at("relation").at(1)) == "delegate_permission/common.handle_all_urls");
    REQUIRE(static_cast<std::string>(doc.root().at(0).at("target").at("package_name")) == "com.example.app");
    REQUIRE(static_cast<std::string>(doc.root().at(0).at("target").at("sha256_cert_fingerprints").at(1)) == "DD:EE:FF");
    REQUIRE(static_cast<std::string>(doc.root().at(0).at("target").at("namespace")) == "android_app");

    // Without link paths only the passkey relations are published.
    c.app_link_paths = {};
    REQUIRE(http::apple_app_site_association(req).body == R"({"webcredentials":{"apps":["ABCDE12345.com.example.app"]}})");
    REQUIRE(http::asset_links(req).body.find("handle_all_urls") == std::string::npos);

    c.app_link_paths = {"/auth/magic"};
    c.apple_app_ids = {};
    c.android_apps = {};
}
