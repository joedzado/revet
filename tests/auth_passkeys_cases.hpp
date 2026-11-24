// Test cases shared by the SQLite and Postgres passkey tests. The including file defines AuthDb (a
// fixture with a `pool` and the users, tokens and passkeys schemas), W and R (Write and Read handle
// types), count(), and AUTH_REQUIRE_DB().
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <revet/auth/passkeys.hpp>
#include <revet/json.hpp>
#include <set>
#include <thread>
#include <vector>

#include "webauthn_soft_authenticator.hpp"

namespace auth = web::auth;
namespace pk = web::auth::passkeys;
namespace wa = web::webauthn;

namespace {

constexpr long long T0 = 1'800'000'000;

void use_test_config() {
    auto& c = pk::config();
    c.rp_name = "Example App";
    c.rp = {.rp_id = "app.example.com", .origins = {"https://app.example.com"}, .require_user_verification = true};
    c.challenge_ttl = std::chrono::seconds(300);
}

auto b64(const std::string& raw) -> std::string {
    return wa::base64url_encode(raw);
}

auto challenge_of(const pk::Options& o) -> std::string {
    revet::json::Document doc(o.options_json);
    return *wa::base64url_decode(static_cast<std::string>(doc.at("challenge")));
}

// Registers `a` as a new user and returns the result (or fails the test).
auto register_new(W& w, soft::Authenticator& a, long long now = T0,
                  const soft::Tweaks& t = {}) -> std::expected<pk::Registered, pk::Failure> {
    const auto opts = pk::begin_registration(w, std::nullopt, "Alex", now);
    REQUIRE(opts.has_value());
    const auto r = a.registration(challenge_of(*opts), t);
    return pk::finish_registration(w, opts->challenge_id, b64(r.client_data_json), b64(r.attestation_object), "iPhone",
                                   now + 1);
}

auto register_add(W& w, const std::string& user, soft::Authenticator& a,
                  long long now = T0) -> std::expected<pk::Registered, pk::Failure> {
    const auto opts = pk::begin_registration(w, user, "Alex", now);
    REQUIRE(opts.has_value());
    const auto r = a.registration(challenge_of(*opts));
    return pk::finish_registration(w, opts->challenge_id, b64(r.client_data_json), b64(r.attestation_object), "Pixel",
                                   now + 1);
}

auto sign_in(W& w, soft::Authenticator& a, long long now = T0 + 100, const soft::Tweaks& t = {},
             const std::string& handle = "") -> std::expected<pk::LoggedIn, pk::Failure> {
    const auto opts = pk::begin_login(w, now);
    const auto r = a.assertion(challenge_of(opts), t);
    return pk::finish_login(w, opts.challenge_id, b64(a.credential_id), b64(r.client_data_json),
                            b64(r.authenticator_data), b64(r.signature), handle.empty() ? "" : b64(handle), now + 1);
}

template<typename T>
auto fails(const std::expected<T, pk::Failure>& r, pk::Failure::Kind k) -> bool {
    return !r.has_value() && r.error().kind == k;
}

auto new_credential(char fill) -> soft::Authenticator {
    soft::Authenticator a;
    a.credential_id = std::string(32, fill);
    return a;
}

} // namespace

TEST_CASE("passkeys: a new user registers and signs in", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');

    const auto reg = register_new(w, a);
    REQUIRE(reg.has_value());
    REQUIRE(reg->new_user);
    REQUIRE(reg->user_id.size() == 32);
    REQUIRE(reg->credential_id == b64(a.credential_id));
    REQUIRE(auth::user_exists(w, reg->user_id));
    REQUIRE(auth::find_user(w, "passkey", reg->credential_id) == reg->user_id);

    const auto in = sign_in(w, a);
    REQUIRE(in.has_value());
    REQUIRE(in->user_id == reg->user_id);
}

TEST_CASE("passkeys: registration options carry what the OS needs", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    const auto opts = pk::begin_registration(w, std::nullopt, "Alex", T0);
    REQUIRE(opts.has_value());
    REQUIRE(opts->challenge_id.size() == 32);

    revet::json::Document doc(opts->options_json);
    REQUIRE(static_cast<std::string>(doc.at("rp").at("id")) == "app.example.com");
    REQUIRE(static_cast<std::string>(doc.at("rp").at("name")) == "Example App");
    REQUIRE(static_cast<std::string>(doc.at("user").at("name")) == "Alex");
    REQUIRE(wa::base64url_decode(static_cast<std::string>(doc.at("user").at("id"))).value().size() == 32);
    REQUIRE(wa::base64url_decode(static_cast<std::string>(doc.at("challenge"))).value().size() == 32);
    REQUIRE(static_cast<int>(doc.at("pubKeyCredParams").at(0).at("alg")) == -7);
    REQUIRE(static_cast<std::string>(doc.at("authenticatorSelection").at("residentKey")) == "required");
    REQUIRE(static_cast<std::string>(doc.at("authenticatorSelection").at("userVerification")) == "required");
    REQUIRE(static_cast<std::string>(doc.at("attestation")) == "none");
    REQUIRE(static_cast<int>(doc.at("timeout")) == 300000);

    // Two ceremonies never share a challenge.
    const auto other = pk::begin_registration(w, std::nullopt, "Alex", T0);
    REQUIRE(challenge_of(*opts) != challenge_of(*other));
}

TEST_CASE("passkeys: login options need no username", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    const auto opts = pk::begin_login(w, T0);
    revet::json::Document doc(opts.options_json);
    REQUIRE(static_cast<std::string>(doc.at("rpId")) == "app.example.com");
    REQUIRE(static_cast<std::string>(doc.at("userVerification")) == "required");
    REQUIRE_FALSE(doc.root().has("allowCredentials"));
    REQUIRE(challenge_of(opts).size() == 32);
}

TEST_CASE("passkeys: a signed-in user adds a second passkey", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto phone = new_credential('a');
    auto laptop = new_credential('b');
    const auto first = register_new(w, phone);
    REQUIRE(first.has_value());

    // The options tell the OS which passkeys not to offer to register again.
    const auto opts = pk::begin_registration(w, first->user_id, "Alex", T0 + 10);
    REQUIRE(opts.has_value());
    revet::json::Document doc(opts->options_json);
    REQUIRE(static_cast<std::string>(doc.at("excludeCredentials").at(0).at("id")) == first->credential_id);
    REQUIRE(wa::base64url_decode(static_cast<std::string>(doc.at("user").at("id"))).value() == first->user_id);

    const auto r = laptop.registration(challenge_of(*opts));
    const auto second = pk::finish_registration(w, opts->challenge_id, b64(r.client_data_json),
                                                b64(r.attestation_object), "MacBook", T0 + 11);
    REQUIRE(second.has_value());
    REQUIRE_FALSE(second->new_user);
    REQUIRE(second->user_id == first->user_id);

    REQUIRE(pk::list_passkeys(w, first->user_id).size() == 2);
    REQUIRE(auth::identities_of(w, first->user_id).size() == 2);
    REQUIRE(sign_in(w, phone, T0 + 100)->user_id == first->user_id);
    REQUIRE(sign_in(w, laptop, T0 + 200)->user_id == first->user_id);
}

TEST_CASE("passkeys: adding a passkey for a user that does not exist", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    REQUIRE(fails(pk::begin_registration(w, std::string(32, 'f'), "Alex", T0), pk::Failure::Kind::no_such_user));
}

TEST_CASE("passkeys: list shows label, times and backup flags", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    const auto reg = register_new(w, a, T0, {.flags = soft::UP | soft::UV | soft::AT | soft::BE | soft::BS});
    REQUIRE(reg.has_value());
    auto list = pk::list_passkeys(w, reg->user_id);
    REQUIRE(list.size() == 1);
    REQUIRE(list[0].credential_id == reg->credential_id);
    REQUIRE(list[0].label == "iPhone");
    REQUIRE(list[0].created_at == T0 + 1);
    REQUIRE(list[0].last_used_at == 0);
    REQUIRE(list[0].backup_eligible);
    REQUIRE(list[0].backup_state);

    REQUIRE(sign_in(w, a, T0 + 500).has_value());
    list = pk::list_passkeys(w, reg->user_id);
    REQUIRE(list[0].last_used_at == T0 + 501);
}

TEST_CASE("passkeys: a challenge answers once", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    const auto opts = pk::begin_registration(w, std::nullopt, "Alex", T0);
    const auto r = a.registration(challenge_of(*opts));
    REQUIRE(
        pk::finish_registration(w, opts->challenge_id, b64(r.client_data_json), b64(r.attestation_object), "x", T0 + 1)
            .has_value());
    // Replaying the same answer finds no challenge.
    REQUIRE(fails(
        pk::finish_registration(w, opts->challenge_id, b64(r.client_data_json), b64(r.attestation_object), "x", T0 + 2),
        pk::Failure::Kind::challenge_invalid));

    const auto login = pk::begin_login(w, T0 + 10);
    const auto ar = a.assertion(challenge_of(login));
    const auto answer = [&] {
        return pk::finish_login(w, login.challenge_id, b64(a.credential_id), b64(ar.client_data_json),
                                b64(ar.authenticator_data), b64(ar.signature), "", T0 + 11);
    };
    REQUIRE(answer().has_value());
    REQUIRE(fails(answer(), pk::Failure::Kind::challenge_invalid)); // a captured login cannot be replayed
}

TEST_CASE("passkeys: a challenge expires", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    const auto opts = pk::begin_registration(w, std::nullopt, "Alex", T0);
    const auto r = a.registration(challenge_of(*opts));
    REQUIRE(fails(pk::finish_registration(w, opts->challenge_id, b64(r.client_data_json), b64(r.attestation_object),
                                          "x", T0 + 300),
                  pk::Failure::Kind::challenge_invalid));
    REQUIRE(count(w, "auth_users") == 0);

    const auto fresh = pk::begin_registration(w, std::nullopt, "Alex", T0);
    const auto r2 = a.registration(challenge_of(*fresh));
    REQUIRE(pk::finish_registration(w, fresh->challenge_id, b64(r2.client_data_json), b64(r2.attestation_object), "x",
                                    T0 + 299)
                .has_value());
}

TEST_CASE("passkeys: expired challenges are cleared when a new ceremony starts", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    for (int i = 0; i < 5; ++i) {
        pk::begin_login(w, T0);
    }
    REQUIRE(count(w, "auth_challenges") == 5);
    pk::begin_login(w, T0 + 1000);
    REQUIRE(count(w, "auth_challenges") == 1);
}

TEST_CASE("passkeys: a challenge only works for the ceremony it was issued for", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    REQUIRE(register_new(w, a).has_value());

    // A login challenge offered to registration.
    const auto login = pk::begin_login(w, T0 + 10);
    auto b = new_credential('b');
    const auto reg = b.registration(challenge_of(login));
    REQUIRE(fails(pk::finish_registration(w, login.challenge_id, b64(reg.client_data_json), b64(reg.attestation_object),
                                          "x", T0 + 11),
                  pk::Failure::Kind::challenge_invalid));

    // A registration challenge offered to login.
    const auto opts = pk::begin_registration(w, std::nullopt, "Alex", T0 + 20);
    const auto ar = a.assertion(challenge_of(*opts));
    REQUIRE(fails(pk::finish_login(w, opts->challenge_id, b64(a.credential_id), b64(ar.client_data_json),
                                   b64(ar.authenticator_data), b64(ar.signature), "", T0 + 21),
                  pk::Failure::Kind::challenge_invalid));
}

TEST_CASE("passkeys: unknown challenge ids are refused", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    const auto r = a.registration(std::string(32, 'x'));
    REQUIRE(fails(
        pk::finish_registration(w, std::string(32, '0'), b64(r.client_data_json), b64(r.attestation_object), "x", T0),
        pk::Failure::Kind::challenge_invalid));
    REQUIRE(fails(pk::finish_registration(w, "", b64(r.client_data_json), b64(r.attestation_object), "x", T0),
                  pk::Failure::Kind::challenge_invalid));
}

TEST_CASE("passkeys: a failed registration spends the challenge and creates nothing", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    const auto opts = pk::begin_registration(w, std::nullopt, "Alex", T0);
    const auto bad = a.registration(challenge_of(*opts), {.origin = "https://evil.example.com"});
    const auto r = pk::finish_registration(w, opts->challenge_id, b64(bad.client_data_json),
                                           b64(bad.attestation_object), "x", T0 + 1);
    REQUIRE(fails(r, pk::Failure::Kind::verification_failed));
    REQUIRE(r.error().detail == wa::Error::wrong_origin);
    REQUIRE(count(w, "auth_users") == 0);
    REQUIRE(count(w, "auth_passkeys") == 0);

    const auto good = a.registration(challenge_of(*opts));
    REQUIRE(fails(pk::finish_registration(w, opts->challenge_id, b64(good.client_data_json),
                                          b64(good.attestation_object), "x", T0 + 2),
                  pk::Failure::Kind::challenge_invalid));
}

TEST_CASE("passkeys: the same credential cannot be registered twice", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    const auto first = register_new(w, a);
    REQUIRE(first.has_value());
    REQUIRE(fails(register_new(w, a, T0 + 50), pk::Failure::Kind::credential_taken));

    auto other = new_credential('z');
    const auto second = register_new(w, other, T0 + 60);
    REQUIRE(second.has_value());
    REQUIRE(fails(register_add(w, second->user_id, a, T0 + 70), pk::Failure::Kind::credential_taken));
    REQUIRE(count(w, "auth_users") == 2);
    REQUIRE(count(w, "auth_passkeys") == 2);
}

TEST_CASE("passkeys: bad input encodings are refused", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    REQUIRE(fails(pk::finish_registration(w, "x", "not base64!", "AAAA", "x", T0), pk::Failure::Kind::bad_request));
    REQUIRE(fails(pk::finish_registration(w, "x", "AAAA", "Zm9v=", "x", T0), pk::Failure::Kind::bad_request));
    REQUIRE(fails(pk::finish_login(w, "x", "", "AAAA", "AAAA", "AAAA", "", T0), pk::Failure::Kind::bad_request));
    REQUIRE(fails(pk::finish_login(w, "x", "cred", "AAAA", "AAAA", "!!", "", T0), pk::Failure::Kind::bad_request));
    REQUIRE(fails(pk::finish_login(w, "x", "cred", "AAAA", "AAAA", "AAAA", "***", T0), pk::Failure::Kind::bad_request));
}

TEST_CASE("passkeys: sign in with an unknown credential", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto known = new_credential('a');
    REQUIRE(register_new(w, known).has_value());
    auto stranger = new_credential('s');
    REQUIRE(fails(sign_in(w, stranger), pk::Failure::Kind::unknown_credential));
}

TEST_CASE("passkeys: a signature from another key is refused", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto real = new_credential('a');
    REQUIRE(register_new(w, real).has_value());

    soft::Authenticator impostor; // same credential id, different key
    impostor.credential_id = real.credential_id;
    const auto r = sign_in(w, impostor);
    REQUIRE(fails(r, pk::Failure::Kind::verification_failed));
    REQUIRE(r.error().detail == wa::Error::bad_signature);
}

TEST_CASE("passkeys: the user handle, when sent, must match the owner", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    const auto reg = register_new(w, a);
    REQUIRE(reg.has_value());
    REQUIRE(sign_in(w, a, T0 + 100, {}, reg->user_id).has_value());
    REQUIRE(fails(sign_in(w, a, T0 + 200, {}, std::string(32, 'e')), pk::Failure::Kind::user_mismatch));
}

TEST_CASE("passkeys: the signature counter is stored and enforced", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    REQUIRE(register_new(w, a).has_value());
    const auto cid = b64(a.credential_id);

    REQUIRE(sign_in(w, a, T0 + 100).has_value());
    REQUIRE(sign_in(w, a, T0 + 200).has_value());
    REQUIRE(w.scalar<long long>("SELECT sign_count FROM auth_passkeys WHERE credential_id = ?", cid) == 2);

    // A clone that kept an old counter is caught.
    const auto r = sign_in(w, a, T0 + 300, {.sign_count = 1});
    REQUIRE(fails(r, pk::Failure::Kind::verification_failed));
    REQUIRE(r.error().detail == wa::Error::sign_count_regressed);
    REQUIRE(w.scalar<long long>("SELECT sign_count FROM auth_passkeys WHERE credential_id = ?", cid) == 2);
    REQUIRE(sign_in(w, a, T0 + 400, {.sign_count = 3}).has_value());
}

TEST_CASE("passkeys: a synced passkey that always reports 0 keeps working", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    REQUIRE(register_new(w, a).has_value());
    for (int i = 0; i < 3; ++i) {
        REQUIRE(sign_in(w, a, T0 + 100 + i * 10, {.sign_count = 0}).has_value());
    }
}

TEST_CASE("passkeys: removing passkeys never locks the user out", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto phone = new_credential('a');
    auto laptop = new_credential('b');
    const auto first = register_new(w, phone);
    REQUIRE(first.has_value());

    REQUIRE(pk::remove_passkey(w, first->user_id, first->credential_id) == pk::RemoveResult::last_login_method);
    REQUIRE(pk::list_passkeys(w, first->user_id).size() == 1);

    const auto second = register_add(w, first->user_id, laptop, T0 + 50);
    REQUIRE(second.has_value());
    REQUIRE(pk::remove_passkey(w, first->user_id, first->credential_id) == pk::RemoveResult::removed);
    REQUIRE(pk::list_passkeys(w, first->user_id).size() == 1);
    REQUIRE(fails(sign_in(w, phone, T0 + 100), pk::Failure::Kind::unknown_credential));
    REQUIRE(sign_in(w, laptop, T0 + 200).has_value());
    REQUIRE(pk::remove_passkey(w, first->user_id, second->credential_id) == pk::RemoveResult::last_login_method);
}

TEST_CASE("passkeys: another login method lets the last passkey go", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    const auto reg = register_new(w, a);
    REQUIRE(reg.has_value());
    REQUIRE(auth::link_identity(w, reg->user_id, "google", "g-1") == auth::LinkResult::linked);
    REQUIRE(pk::remove_passkey(w, reg->user_id, reg->credential_id) == pk::RemoveResult::removed);
    REQUIRE(pk::list_passkeys(w, reg->user_id).empty());
    REQUIRE_FALSE(auth::find_user(w, "passkey", reg->credential_id).has_value());
}

TEST_CASE("passkeys: removing someone else's passkey does nothing", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    auto b = new_credential('b');
    const auto alice = register_new(w, a);
    const auto bob = register_new(w, b, T0 + 50);
    REQUIRE(pk::remove_passkey(w, bob->user_id, alice->credential_id) == pk::RemoveResult::not_found);
    REQUIRE(pk::remove_passkey(w, alice->user_id, "no-such-credential") == pk::RemoveResult::not_found);
    REQUIRE(sign_in(w, a, T0 + 100).has_value());
}

TEST_CASE("passkeys: deleting a user removes their passkeys", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    const auto reg = register_new(w, a);
    REQUIRE(auth::delete_user(w, reg->user_id));
    REQUIRE(count(w, "auth_passkeys") == 0);
    REQUIRE(fails(sign_in(w, a), pk::Failure::Kind::unknown_credential));
}

TEST_CASE("passkeys: sign in feeds the token layer", "[auth][db]") {
    AUTH_REQUIRE_DB();
    use_test_config();
    AuthDb fx;
    W w(fx.pool);
    auto a = new_credential('a');
    REQUIRE(register_new(w, a).has_value());
    const auto in = sign_in(w, a);
    REQUIRE(in.has_value());
    const auto pair = auth::tokens::issue(w, in->user_id, "iPhone");
    REQUIRE(auth::tokens::verify_access(pair.access_token)->user_id == in->user_id);
}
