// Tests for web::webauthn: verification of what an authenticator sends. Two sources of messages:
// fixed vectors made by tests/gen_webauthn_vectors.py (openssl CLI + a separate CBOR encoder), and a
// software authenticator (tests/webauthn_soft_authenticator.hpp) with knobs to break each field.
#include <catch2/catch_test_macros.hpp>
#include <revet/webauthn.hpp>

#include "webauthn_soft_authenticator.hpp"
#include "webauthn_vectors.hpp"

namespace wa = web::webauthn;
using namespace std::string_literals;

namespace {

const std::string challenge = "0123456789abcdef0123456789abcdef";

auto config() -> wa::Config {
    return {.rp_id = "app.example.com",
            .origins = {"https://app.example.com", "android:apk-key-hash:3q2-7wARIjNEVWZ3iJmqu8zd7v8"}};
}

// Registers a fresh software authenticator and returns it with its stored credential.
struct Enrolled {
    soft::Authenticator auth;
    wa::Registration reg;
};
auto enroll(const soft::Tweaks& t = {}) -> std::expected<Enrolled, wa::Error> {
    Enrolled e;
    const auto r = e.auth.registration(challenge, t);
    auto v = wa::verify_registration(config(), challenge, r.client_data_json, r.attestation_object);
    if (!v) return std::unexpected(v.error());
    e.reg = *v;
    return e;
}

auto login(Enrolled& e, const soft::Tweaks& t = {}, std::uint32_t stored = 0, const std::string& chal = challenge)
    -> std::expected<wa::Assertion, wa::Error> {
    const auto a = e.auth.assertion(chal, t);
    return wa::verify_assertion(config(), challenge, e.reg.public_key, stored, a.client_data_json, a.authenticator_data, a.signature);
}

template<typename T>
auto fails_with(const std::expected<T, wa::Error>& r, wa::Error e) -> bool {
    return !r.has_value() && r.error() == e;
}

} // namespace

// ── base64url ────────────────────────────────────────────────────────────────

TEST_CASE("base64url: round trip without padding", "[webauthn]") {
    for (const std::string s : {""s, "f"s, "fo"s, "foo"s, "foob"s, "\xff\xfe\xfd"s, "\x00\x01"s}) {
        const auto enc = wa::base64url_encode(s);
        REQUIRE(enc.find('=') == std::string::npos);
        REQUIRE(wa::base64url_decode(enc) == s);
    }
    REQUIRE(wa::base64url_encode("\xfb\xff\xbf"s) == "-_-_");
}

TEST_CASE("base64url: strict decoding", "[webauthn]") {
    REQUIRE_FALSE(wa::base64url_decode("Zm9v=").has_value());   // padding
    REQUIRE_FALSE(wa::base64url_decode("Zm+/").has_value());    // standard alphabet
    REQUIRE_FALSE(wa::base64url_decode("Zm9v ").has_value());   // whitespace
    REQUIRE_FALSE(wa::base64url_decode("Zm9\n").has_value());
    REQUIRE_FALSE(wa::base64url_decode("Z").has_value());       // impossible length
    REQUIRE_FALSE(wa::base64url_decode("Zh").has_value());      // non-zero trailing bits
}

// ── known-answer vectors ─────────────────────────────────────────────────────

TEST_CASE("webauthn: a registration made outside this code verifies", "[webauthn]") {
    namespace v = webauthn_vectors;
    const wa::Config cfg{.rp_id = v::rp_id, .origins = {v::origin}};
    const auto r = wa::verify_registration(cfg, v::reg_challenge, v::reg_client_data_json, v::reg_attestation_object);
    REQUIRE(r.has_value());
    REQUIRE(r->credential_id == v::credential_id);
    REQUIRE(r->public_key == v::public_key);
    REQUIRE(r->sign_count == 0);
    REQUIRE_FALSE(r->backup_eligible);
}

TEST_CASE("webauthn: a login signed by the openssl tool verifies", "[webauthn]") {
    namespace v = webauthn_vectors;
    const wa::Config cfg{.rp_id = v::rp_id, .origins = {v::origin}};
    const auto a = wa::verify_assertion(cfg, v::get_challenge, v::public_key, 0, v::get_client_data_json, v::get_authenticator_data,
                                        v::get_signature);
    REQUIRE(a.has_value());
    REQUIRE(a->sign_count == v::get_sign_count);

    // The same message does not verify for a different challenge or after a bit flips.
    REQUIRE(fails_with(wa::verify_assertion(cfg, v::reg_challenge, v::public_key, 0, v::get_client_data_json, v::get_authenticator_data,
                                            v::get_signature),
                       wa::Error::wrong_challenge));
    auto sig = v::get_signature;
    sig[sig.size() - 1] ^= 0x01;
    REQUIRE(fails_with(wa::verify_assertion(cfg, v::get_challenge, v::public_key, 0, v::get_client_data_json, v::get_authenticator_data, sig),
                       wa::Error::bad_signature));
}

// ── registration ─────────────────────────────────────────────────────────────

TEST_CASE("webauthn: registration round trip", "[webauthn]") {
    const auto e = enroll();
    REQUIRE(e.has_value());
    REQUIRE(e->reg.credential_id == e->auth.credential_id);
    REQUIRE(e->reg.public_key == e->auth.key.point);
    REQUIRE(e->reg.public_key.size() == 65);
    REQUIRE(e->reg.aaguid.size() == 16);
}

TEST_CASE("webauthn: registration reports backup flags", "[webauthn]") {
    const auto synced = enroll({.flags = soft::UP | soft::UV | soft::AT | soft::BE | soft::BS});
    REQUIRE(synced.has_value());
    REQUIRE(synced->reg.backup_eligible);
    REQUIRE(synced->reg.backup_state);
    REQUIRE(fails_with(enroll({.flags = soft::UP | soft::UV | soft::AT | soft::BS}), wa::Error::bad_backup_flags));
}

TEST_CASE("webauthn: any attestation format is accepted, statement unchecked", "[webauthn]") {
    for (const char* fmt : {"none", "packed", "apple", "android-key", "fido-u2f", "tpm"}) {
        REQUIRE(enroll({.fmt = fmt}).has_value());
    }
}

TEST_CASE("webauthn: registration from an Android app origin", "[webauthn]") {
    REQUIRE(enroll({.origin = "android:apk-key-hash:3q2-7wARIjNEVWZ3iJmqu8zd7v8"}).has_value());
    REQUIRE(fails_with(enroll({.origin = "android:apk-key-hash:someone-elses-app"}), wa::Error::wrong_origin));
}

TEST_CASE("webauthn: registration is refused for every wrong field", "[webauthn]") {
    REQUIRE(fails_with(enroll({.type = "webauthn.get"}), wa::Error::wrong_type));
    REQUIRE(fails_with(enroll({.challenge_text = wa::base64url_encode("another challenge of 32 bytes!!!!!!")}), wa::Error::wrong_challenge));
    REQUIRE(fails_with(enroll({.challenge_text = "!!not base64!!"}), wa::Error::wrong_challenge));
    REQUIRE(fails_with(enroll({.origin = "https://evil.example.com"}), wa::Error::wrong_origin));
    REQUIRE(fails_with(enroll({.origin = "https://app.example.com.evil.com"}), wa::Error::wrong_origin));
    REQUIRE(fails_with(enroll({.origin = "http://app.example.com"}), wa::Error::wrong_origin));
    REQUIRE(fails_with(enroll({.cross_origin = true}), wa::Error::cross_origin));
    REQUIRE(fails_with(enroll({.rp_id = "evil.example.com"}), wa::Error::wrong_rp_id));
    REQUIRE(fails_with(enroll({.flags = soft::UV | soft::AT}), wa::Error::user_not_present));
    REQUIRE(fails_with(enroll({.flags = soft::UP | soft::AT}), wa::Error::user_not_verified));
    REQUIRE(fails_with(enroll({.flags = soft::UP | soft::UV}), wa::Error::bad_credential_data)); // no attested credential
    REQUIRE(fails_with(enroll({.break_cose = true}), wa::Error::unsupported_algorithm));
}

TEST_CASE("webauthn: user verification can be made optional", "[webauthn]") {
    auto cfg = config();
    cfg.require_user_verification = false;
    soft::Authenticator a;
    const auto r = a.registration(challenge, {.flags = soft::UP | soft::AT});
    REQUIRE(wa::verify_registration(cfg, challenge, r.client_data_json, r.attestation_object).has_value());
    REQUIRE(fails_with(wa::verify_registration(config(), challenge, r.client_data_json, r.attestation_object), wa::Error::user_not_verified));
}

TEST_CASE("webauthn: a challenge shorter than 16 bytes is never trusted", "[webauthn]") {
    soft::Authenticator a;
    const std::string weak = "short";
    const auto r = a.registration(weak);
    REQUIRE(fails_with(wa::verify_registration(config(), weak, r.client_data_json, r.attestation_object), wa::Error::wrong_challenge));
    REQUIRE(fails_with(wa::verify_registration(config(), "", r.client_data_json, r.attestation_object), wa::Error::wrong_challenge));
}

TEST_CASE("webauthn: no origin is accepted when none are configured", "[webauthn]") {
    soft::Authenticator a;
    const auto r = a.registration(challenge);
    const wa::Config none{.rp_id = "app.example.com", .origins = {}};
    REQUIRE(fails_with(wa::verify_registration(none, challenge, r.client_data_json, r.attestation_object), wa::Error::wrong_origin));
}

TEST_CASE("webauthn: a public key that is not on the curve is refused", "[webauthn]") {
    soft::Authenticator a;
    a.key.point = std::string(1, '\x04') + std::string(32, '\x01') + std::string(32, '\x02'); // (1, 2) is not a P-256 point
    const auto r = a.registration(challenge);
    REQUIRE(fails_with(wa::verify_registration(config(), challenge, r.client_data_json, r.attestation_object), wa::Error::bad_public_key));
}

TEST_CASE("webauthn: malformed registration inputs are errors, never crashes", "[webauthn]") {
    soft::Authenticator a;
    const auto r = a.registration(challenge);
    const auto call = [&](const std::string& cdj, const std::string& att) {
        return wa::verify_registration(config(), challenge, cdj, att);
    };
    REQUIRE(fails_with(call("", r.attestation_object), wa::Error::bad_client_data));
    REQUIRE(fails_with(call("not json", r.attestation_object), wa::Error::bad_client_data));
    REQUIRE(fails_with(call("[]", r.attestation_object), wa::Error::bad_client_data));
    REQUIRE(fails_with(call(R"({"type":"webauthn.create"})", r.attestation_object), wa::Error::bad_client_data));
    REQUIRE(fails_with(call(R"({"type":5,"challenge":"x","origin":"y"})", r.attestation_object), wa::Error::bad_client_data));
    REQUIRE(fails_with(call(r.client_data_json, ""), wa::Error::bad_attestation));
    REQUIRE(fails_with(call(r.client_data_json, "garbage"), wa::Error::bad_attestation));
    REQUIRE(fails_with(call(r.client_data_json, r.attestation_object + "x"), wa::Error::bad_attestation));
    REQUIRE(fails_with(call(r.client_data_json, soft::map({{soft::tstr("fmt"), soft::tstr("none")}})), wa::Error::bad_attestation));
    REQUIRE(fails_with(call(r.client_data_json, soft::uint(1)), wa::Error::bad_attestation));

    // Cutting the attestation object short anywhere must fail cleanly.
    for (std::size_t n = 0; n < r.attestation_object.size(); ++n) {
        REQUIRE_FALSE(call(r.client_data_json, r.attestation_object.substr(0, n)).has_value());
    }
}

// ── assertion ────────────────────────────────────────────────────────────────

TEST_CASE("webauthn: login round trip", "[webauthn]") {
    auto e = enroll();
    REQUIRE(e.has_value());
    const auto a = login(*e);
    REQUIRE(a.has_value());
    REQUIRE(a->sign_count == 1);
}

TEST_CASE("webauthn: the counter must advance", "[webauthn]") {
    auto e = enroll();
    REQUIRE(e.has_value());
    std::uint32_t stored = 0;
    for (int i = 0; i < 3; ++i) {
        const auto a = login(*e, {}, stored);
        REQUIRE(a.has_value());
        REQUIRE(a->sign_count > stored);
        stored = a->sign_count;
    }
    REQUIRE(fails_with(login(*e, {.sign_count = 3}, stored), wa::Error::sign_count_regressed)); // equal
    REQUIRE(fails_with(login(*e, {.sign_count = 1}, stored), wa::Error::sign_count_regressed)); // lower
    REQUIRE(fails_with(login(*e, {.sign_count = 0}, stored), wa::Error::sign_count_regressed)); // reset to zero
    REQUIRE(login(*e, {.sign_count = 4}, stored).has_value());
}

TEST_CASE("webauthn: a counter that never moves is allowed", "[webauthn]") {
    auto e = enroll();
    REQUIRE(e.has_value());
    REQUIRE(login(*e, {.sign_count = 0}, 0).has_value()); // synced passkeys report 0 every time
    REQUIRE(login(*e, {.sign_count = 0}, 0).has_value());
}

TEST_CASE("webauthn: login is refused for every wrong field", "[webauthn]") {
    auto e = enroll();
    REQUIRE(e.has_value());
    REQUIRE(fails_with(login(*e, {.type = "webauthn.create"}), wa::Error::wrong_type));
    REQUIRE(fails_with(login(*e, {.origin = "https://evil.example.com"}), wa::Error::wrong_origin));
    REQUIRE(fails_with(login(*e, {.cross_origin = true}), wa::Error::cross_origin));
    REQUIRE(fails_with(login(*e, {.rp_id = "evil.example.com"}), wa::Error::wrong_rp_id));
    REQUIRE(fails_with(login(*e, {.flags = soft::UV}), wa::Error::user_not_present));
    REQUIRE(fails_with(login(*e, {.flags = soft::UP}), wa::Error::user_not_verified));
    REQUIRE(fails_with(login(*e, {}, 0, "a different challenge of 32 bytes!!"), wa::Error::wrong_challenge));
    REQUIRE(fails_with(login(*e, {.flags = soft::UP | soft::UV | soft::BS}), wa::Error::bad_backup_flags));
}

TEST_CASE("webauthn: a signature from another key is refused", "[webauthn]") {
    auto e = enroll();
    REQUIRE(e.has_value());
    soft::Authenticator other;
    other.credential_id = e->auth.credential_id;
    const auto a = other.assertion(challenge);
    REQUIRE(fails_with(wa::verify_assertion(config(), challenge, e->reg.public_key, 0, a.client_data_json, a.authenticator_data, a.signature),
                       wa::Error::bad_signature));
}

TEST_CASE("webauthn: tampering after signing is caught", "[webauthn]") {
    auto e = enroll();
    REQUIRE(e.has_value());
    const auto a = e->auth.assertion(challenge);
    const auto verify = [&](const std::string& cdj, const std::string& ad, const std::string& sig) {
        return wa::verify_assertion(config(), challenge, e->reg.public_key, 0, cdj, ad, sig);
    };
    REQUIRE(verify(a.client_data_json, a.authenticator_data, a.signature).has_value());

    auto cdj = a.client_data_json;
    cdj.insert(cdj.size() - 1, R"(,"extra":1)"); // still valid JSON, but not what was signed
    REQUIRE(fails_with(verify(cdj, a.authenticator_data, a.signature), wa::Error::bad_signature));

    auto ad = a.authenticator_data;
    ad[ad.size() - 1] ^= 0x01; // counter changed after signing
    REQUIRE(fails_with(verify(a.client_data_json, ad, a.signature), wa::Error::bad_signature));

    auto sig = a.signature;
    sig[sig.size() / 2] ^= 0x40;
    REQUIRE_FALSE(verify(a.client_data_json, a.authenticator_data, sig).has_value());
    REQUIRE_FALSE(verify(a.client_data_json, a.authenticator_data, "").has_value());
    REQUIRE_FALSE(verify(a.client_data_json, a.authenticator_data, a.signature.substr(0, a.signature.size() - 1)).has_value());
    REQUIRE_FALSE(verify(a.client_data_json, a.authenticator_data, a.signature + "x").has_value());
}

TEST_CASE("webauthn: bad stored public keys are refused", "[webauthn]") {
    auto e = enroll();
    REQUIRE(e.has_value());
    const auto a = e->auth.assertion(challenge);
    const auto verify = [&](const std::string& key) {
        return wa::verify_assertion(config(), challenge, key, 0, a.client_data_json, a.authenticator_data, a.signature);
    };
    REQUIRE(fails_with(verify(""), wa::Error::bad_public_key));
    REQUIRE(fails_with(verify(e->reg.public_key.substr(1)), wa::Error::bad_public_key));        // compressed form missing prefix
    REQUIRE(fails_with(verify("\x02"s + e->reg.public_key.substr(1, 32)), wa::Error::bad_public_key)); // compressed
    REQUIRE(fails_with(verify(std::string(65, '\0')), wa::Error::bad_public_key));
    REQUIRE(fails_with(verify("\x04"s + std::string(64, '\x07')), wa::Error::bad_public_key));
}

TEST_CASE("webauthn: malformed login inputs are errors, never crashes", "[webauthn]") {
    auto e = enroll();
    REQUIRE(e.has_value());
    const auto a = e->auth.assertion(challenge);
    for (std::size_t n = 0; n < a.authenticator_data.size(); ++n) {
        const auto r = wa::verify_assertion(config(), challenge, e->reg.public_key, 0, a.client_data_json,
                                            a.authenticator_data.substr(0, n), a.signature);
        REQUIRE(fails_with(r, wa::Error::bad_auth_data));
    }
    // Extra bytes after authenticator data, with no extension flag.
    REQUIRE(fails_with(wa::verify_assertion(config(), challenge, e->reg.public_key, 0, a.client_data_json, a.authenticator_data + "x", a.signature),
                       wa::Error::bad_auth_data));
    for (std::size_t n = 0; n < a.client_data_json.size(); n += 3) {
        REQUIRE_FALSE(wa::verify_assertion(config(), challenge, e->reg.public_key, 0, a.client_data_json.substr(0, n), a.authenticator_data,
                                           a.signature)
                          .has_value());
    }
}

TEST_CASE("webauthn: credential ids longer than 1023 bytes are refused", "[webauthn]") {
    soft::Authenticator a;
    a.credential_id = std::string(1024, '\x22');
    const auto r = a.registration(challenge);
    REQUIRE(fails_with(wa::verify_registration(config(), challenge, r.client_data_json, r.attestation_object), wa::Error::bad_credential_data));
    a.credential_id = std::string(1023, '\x22');
    const auto ok = a.registration(challenge);
    REQUIRE(wa::verify_registration(config(), challenge, ok.client_data_json, ok.attestation_object).has_value());
}

TEST_CASE("webauthn: error names exist for logging", "[webauthn]") {
    REQUIRE(wa::error_name(wa::Error::bad_signature) == "bad_signature");
    REQUIRE(wa::error_name(wa::Error::sign_count_regressed) == "sign_count_regressed");
}
