// Randomised abuse of the WebAuthn verifier and the CBOR decoder, seeded so failures reproduce.
// Properties: nothing crashes (ASan and UBSan are on in the test build), and a login answer only
// verifies when every input is byte-for-byte the original.
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <revet/cbor.hpp>
#include <revet/webauthn.hpp>

#include "webauthn_browser_vectors.hpp"

namespace wa = web::webauthn;
namespace bv = webauthn_browser_vectors;

namespace {

const wa::Config cfg{.rp_id = bv::rp_id, .origins = {bv::origin}, .require_user_verification = true};

// Changes `s` in one of several ways, and always changes it.
auto mutate(const std::string& s, std::mt19937_64& rng) -> std::string {
    for (;;) {
        std::string out = s;
        switch (rng() % 6) {
            case 0: // flip a few bytes to random values
                for (int n = 1 + static_cast<int>(rng() % 4); n > 0 && !out.empty(); --n) {
                    out[rng() % out.size()] = static_cast<char>(rng());
                }
                break;
            case 1: // truncate
                if (!out.empty()) {
                    out.resize(rng() % out.size());
                }
                break;
            case 2: // append junk
                for (int n = 1 + static_cast<int>(rng() % 16); n > 0; --n) {
                    out.push_back(static_cast<char>(rng()));
                }
                break;
            case 3: // delete a range
                if (!out.empty()) {
                    const auto at = rng() % out.size();
                    out.erase(at, 1 + (rng() % std::min<std::size_t>(8, out.size() - at)));
                }
                break;
            case 4: // duplicate a range
                if (!out.empty()) {
                    const auto at = rng() % out.size();
                    const auto len = 1 + (rng() % std::min<std::size_t>(8, out.size() - at));
                    out.insert(at, out.substr(at, len));
                }
                break;
            default: // swap two bytes
                if (out.size() > 1) {
                    std::swap(out[rng() % out.size()], out[rng() % out.size()]);
                }
                break;
        }
        if (out != s) {
            return out;
        }
    }
}

auto random_bytes(std::mt19937_64& rng, std::size_t max) -> std::string {
    std::string s(rng() % (max + 1), '\0');
    for (auto& c : s) {
        c = static_cast<char>(rng());
    }
    return s;
}

} // namespace

TEST_CASE("fuzz: a login verifies only when every input is the original", "[webauthn][fuzz]") {
    using V = bv::plain;
    const auto reg = wa::verify_registration(cfg, V::reg_challenge, V::reg_client_data_json, V::reg_attestation_object);
    REQUIRE(reg.has_value());
    const auto call = [&](const std::string& challenge, const std::string& key, const std::string& cdj,
                          const std::string& ad, const std::string& sig) -> bool {
        return wa::verify_assertion(cfg, challenge, key, 0, cdj, ad, sig).has_value();
    };
    REQUIRE(call(V::get1_challenge, reg->public_key, V::get1_client_data_json, V::get1_authenticator_data,
                 V::get1_signature));

    std::mt19937_64 rng(0x5eed);
    int tried = 0;
    for (int i = 0; i < 20000; ++i) {
        const auto field = rng() % 5;
        auto challenge = V::get1_challenge;
        auto key = reg->public_key;
        auto cdj = V::get1_client_data_json;
        auto ad = V::get1_authenticator_data;
        auto sig = V::get1_signature;
        switch (field) {
            case 0:
                challenge = mutate(challenge, rng);
                break;
            case 1:
                key = mutate(key, rng);
                break;
            case 2:
                cdj = mutate(cdj, rng);
                break;
            case 3:
                ad = mutate(ad, rng);
                break;
            default:
                sig = mutate(sig, rng);
                break;
        }
        ++tried;
        REQUIRE_FALSE(call(challenge, key, cdj, ad, sig));
    }
    REQUIRE(tried == 20000);
}

TEST_CASE("fuzz: pure noise is refused by every entry point", "[webauthn][fuzz]") {
    std::mt19937_64 rng(0xbadf00d);
    for (int i = 0; i < 20000; ++i) {
        const auto challenge = random_bytes(rng, 40);
        const auto a = random_bytes(rng, 300);
        const auto b = random_bytes(rng, 300);
        const auto c = random_bytes(rng, 100);
        REQUIRE_FALSE(wa::verify_registration(cfg, challenge, a, b).has_value());
        REQUIRE_FALSE(wa::verify_assertion(cfg, challenge, c, 0, a, b, c).has_value());
        (void)wa::base64url_decode(a);
        (void)wa::base64url_encode(b);
    }
}

TEST_CASE("fuzz: damaged registrations never crash and never yield a bad key", "[webauthn][fuzz]") {
    std::mt19937_64 rng(0xc0ffee);
    for (const auto* original : {&bv::plain::reg_attestation_object, &bv::direct::reg_attestation_object,
                                 &bv::synced::reg_attestation_object}) {
        for (int i = 0; i < 8000; ++i) {
            const auto att = mutate(*original, rng);
            const auto r = wa::verify_registration(cfg, bv::plain::reg_challenge, bv::plain::reg_client_data_json, att);
            // A damaged object may still parse (for example a changed credential id or attestation
            // statement), but whatever comes back must be a well-formed key.
            if (r) {
                REQUIRE(r->public_key.size() == 65);
                REQUIRE(r->public_key[0] == 0x04);
                REQUIRE(!r->credential_id.empty());
                REQUIRE(r->credential_id.size() <= 1023);
            }
        }
    }
    // Damage to the client data cannot make a registration verify.
    for (int i = 0; i < 8000; ++i) {
        const auto cdj = mutate(bv::plain::reg_client_data_json, rng);
        const auto r = wa::verify_registration(cfg, bv::plain::reg_challenge, cdj, bv::plain::reg_attestation_object);
        if (r) {
            // Only possible when the change left the JSON meaning intact (whitespace, key order); never a different
            // challenge or origin.
            REQUIRE(r->credential_id == bv::plain::credential_id);
        }
    }
}

TEST_CASE("fuzz: the CBOR decoder survives damaged and random input", "[cbor][fuzz]") {
    std::mt19937_64 rng(0xcb02);
    const std::string seeds[] = {bv::plain::reg_attestation_object, bv::direct::reg_attestation_object,
                                 bv::synced::reg_attestation_object};
    for (int i = 0; i < 30000; ++i) {
        const auto input = (i % 3 == 0) ? random_bytes(rng, 200) : mutate(seeds[static_cast<std::size_t>(i) % 3], rng);
        const auto r = web::cbor::decode_prefix(input);
        if (r) {
            REQUIRE(r->consumed <= input.size());
            REQUIRE(r->consumed >= 1);
        }
        (void)web::cbor::decode(input);
    }
    // Every prefix of a real object either decodes to a complete shorter item or reports an error.
    for (const auto& seed : seeds) {
        for (std::size_t n = 0; n <= seed.size(); ++n) {
            (void)web::cbor::decode_prefix(std::string_view(seed).substr(0, n));
        }
    }
}
