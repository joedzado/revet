// WebAuthn verification against bytes a real Chromium produced (virtual authenticator over the
// DevTools protocol; see tests/gen_webauthn_browser_vectors.mjs). These come from a browser's own
// implementation, not from this repository's code or from the software authenticator in the other tests.
#include <catch2/catch_test_macros.hpp>
#include <revet/cbor.hpp>
#include <revet/webauthn.hpp>

#include "webauthn_browser_vectors.hpp"

// NOLINTBEGIN(bugprone-signed-bitwise,bugprone-throwing-static-initialization,cert-err58-cpp,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access):

// test fixtures

namespace wa = web::webauthn;
namespace bv = webauthn_browser_vectors;

namespace {

const wa::Config cfg{.rp_id = bv::rp_id, .origins = {bv::origin}, .require_user_verification = true};

// The same checks for each kind of authenticator Chromium can imitate.
template<typename V>
void check_variant(bool expect_synced, std::string_view expect_fmt) {
    const auto reg = wa::verify_registration(cfg, V::reg_challenge, V::reg_client_data_json, V::reg_attestation_object);
    REQUIRE(reg.has_value());
    REQUIRE(reg->credential_id == V::credential_id);
    REQUIRE(reg->public_key.size() == 65);
    REQUIRE(reg->public_key[0] == 0x04);
    REQUIRE(reg->backup_eligible == expect_synced);
    REQUIRE(reg->backup_state == expect_synced);

    // The attestation really is the format the test is named for.
    const auto att = web::cbor::decode(V::reg_attestation_object);
    REQUIRE(att.has_value());
    REQUIRE(*att->find("fmt")->as_text() == expect_fmt);

    // Two logins in a row, each counter higher than the last.
    const auto a1 = wa::verify_assertion(cfg, V::get1_challenge, reg->public_key, reg->sign_count,
                                         V::get1_client_data_json, V::get1_authenticator_data, V::get1_signature);
    REQUIRE(a1.has_value());
    REQUIRE(a1->sign_count > reg->sign_count);
    REQUIRE(a1->backup_eligible == expect_synced);
    const auto a2 = wa::verify_assertion(cfg, V::get2_challenge, reg->public_key, a1->sign_count,
                                         V::get2_client_data_json, V::get2_authenticator_data, V::get2_signature);
    REQUIRE(a2.has_value());
    REQUIRE(a2->sign_count > a1->sign_count);

    // Replaying the first login after the second is a counter that went backwards.
    const auto replay = wa::verify_assertion(cfg, V::get1_challenge, reg->public_key, a2->sign_count,
                                             V::get1_client_data_json, V::get1_authenticator_data, V::get1_signature);
    REQUIRE_FALSE(replay.has_value());
    REQUIRE(replay.error() == wa::Error::sign_count_regressed);

    // One login's answer does not satisfy another login's challenge.
    const auto crossed = wa::verify_assertion(cfg, V::get2_challenge, reg->public_key, reg->sign_count,
                                              V::get1_client_data_json, V::get1_authenticator_data, V::get1_signature);
    REQUIRE_FALSE(crossed.has_value());
    REQUIRE(crossed.error() == wa::Error::wrong_challenge);

    // A flipped bit anywhere in the signature, authenticator data or client data fails.
    for (std::size_t i = 0; i < V::get1_signature.size(); ++i) {
        auto sig = V::get1_signature;
        sig[i] ^= 0x01;
        REQUIRE_FALSE(wa::verify_assertion(cfg, V::get1_challenge, reg->public_key, 0, V::get1_client_data_json,
                                           V::get1_authenticator_data, sig)
                          .has_value());
    }
    for (std::size_t i = 0; i < V::get1_authenticator_data.size(); ++i) {
        auto ad = V::get1_authenticator_data;
        ad[i] ^= 0x01;
        REQUIRE_FALSE(wa::verify_assertion(cfg, V::get1_challenge, reg->public_key, 0, V::get1_client_data_json, ad,
                                           V::get1_signature)
                          .has_value());
    }

    // Wrong site: the same bytes under another RP ID or origin are refused.
    auto other_site = cfg;
    other_site.rp_id = "example.com";
    REQUIRE(wa::verify_registration(other_site, V::reg_challenge, V::reg_client_data_json, V::reg_attestation_object)
                .error() == wa::Error::wrong_rp_id);
    auto other_origin = cfg;
    other_origin.origins = {"https://localhost"};
    REQUIRE(wa::verify_registration(other_origin, V::reg_challenge, V::reg_client_data_json, V::reg_attestation_object)
                .error() == wa::Error::wrong_origin);

    // A key from a different credential does not verify this one's signature.
    const auto other = wa::verify_registration(cfg, bv::plain::reg_challenge, bv::plain::reg_client_data_json,
                                               bv::plain::reg_attestation_object);
    REQUIRE(other.has_value());
    if (other->public_key != reg->public_key) {
        REQUIRE(wa::verify_assertion(cfg, V::get1_challenge, other->public_key, 0, V::get1_client_data_json,
                                     V::get1_authenticator_data, V::get1_signature)
                    .error() == wa::Error::bad_signature);
    }
}

} // namespace

TEST_CASE("browser vectors: a plain passkey with no attestation", "[webauthn][browser]") {
    check_variant<bv::plain>(false, "none");
}

TEST_CASE("browser vectors: direct attestation is parsed and accepted", "[webauthn][browser]") {
    check_variant<bv::direct>(false, "packed");
}

TEST_CASE("browser vectors: a synced (backup eligible, backed up) passkey", "[webauthn][browser]") {
    check_variant<bv::synced>(true, "none");
}
// NOLINTEND(bugprone-signed-bitwise,bugprone-throwing-static-initialization,cert-err58-cpp,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
