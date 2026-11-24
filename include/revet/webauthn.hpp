#pragma once

// WebAuthn (passkey) verification: the checks a relying party runs on what an authenticator sends.
// Pure functions. Bytes in, a verified result or an error out; no database, no HTTP, no state.
// Challenges, sign counts and credential storage belong to the caller (see auth/passkeys.hpp).
//
//   web::webauthn::Config cfg{.rp_id = "app.example.com",
//                             .origins = {"https://app.example.com",
//                                         "android:apk-key-hash:<base64url sha256 of your signing cert>"}};
//
//   // Registration: the app sends clientDataJSON and attestationObject (base64url-decode them first).
//   auto reg = web::webauthn::verify_registration(cfg, challenge, client_data_json, attestation_object);
//   // store reg->credential_id, reg->public_key, reg->sign_count
//
//   // Login: clientDataJSON, authenticatorData, signature.
//   auto ok = web::webauthn::verify_assertion(cfg, challenge, stored_public_key, stored_sign_count,
//                                             client_data_json, authenticator_data, signature);
//
// Supported: ES256 (ECDSA over P-256 with SHA-256), which iPhones, Android phones and most security
// keys use. Attestation statements are parsed but not validated (any `fmt` is accepted), so the
// result proves the credential belongs to this site, not which make of authenticator holds it.
//
// Needs OpenSSL for the signature check; the includes live in src/webauthn.cpp.

#include <cstdint>
#include <expected>
#include <optional>
#include <sodium.h>
#include <string>
#include <string_view>
#include <vector>

namespace web::webauthn {

enum class Error : std::uint8_t {
    bad_client_data,       // clientDataJSON is not the expected JSON object
    wrong_type,            // webauthn.create where webauthn.get was expected, or the reverse
    wrong_challenge,       // does not match the challenge we issued (or ours was too short to trust)
    wrong_origin,          // origin is not on the allowed list
    cross_origin,          // the browser says it was embedded in another origin
    bad_attestation,       // attestationObject is not the expected CBOR
    bad_auth_data,         // authenticator data is malformed
    wrong_rp_id,           // RP ID hash is not ours
    user_not_present,      // the user-present flag is missing
    user_not_verified,     // user verification (PIN, biometrics) required and missing
    bad_backup_flags,      // backed-up flag set on a credential that is not backup eligible
    bad_credential_data,   // missing or oversized credential id
    unsupported_algorithm, // public key is not ES256 over P-256
    bad_public_key,        // not a valid point on the curve
    bad_signature,         // signature does not verify
    sign_count_regressed,  // counter did not advance: the credential may have been cloned
};

[[nodiscard]] constexpr auto error_name(Error e) -> std::string_view {
    switch (e) {
        case Error::bad_client_data:
            return "bad_client_data";
        case Error::wrong_type:
            return "wrong_type";
        case Error::wrong_challenge:
            return "wrong_challenge";
        case Error::wrong_origin:
            return "wrong_origin";
        case Error::cross_origin:
            return "cross_origin";
        case Error::bad_attestation:
            return "bad_attestation";
        case Error::bad_auth_data:
            return "bad_auth_data";
        case Error::wrong_rp_id:
            return "wrong_rp_id";
        case Error::user_not_present:
            return "user_not_present";
        case Error::user_not_verified:
            return "user_not_verified";
        case Error::bad_backup_flags:
            return "bad_backup_flags";
        case Error::bad_credential_data:
            return "bad_credential_data";
        case Error::unsupported_algorithm:
            return "unsupported_algorithm";
        case Error::bad_public_key:
            return "bad_public_key";
        case Error::bad_signature:
            return "bad_signature";
        case Error::sign_count_regressed:
            return "sign_count_regressed";
    }
    return "unknown";
}

struct Config {
    std::string rp_id;                     // the site's domain, e.g. "app.example.com"
    std::vector<std::string> origins;      // exact origins accepted: "https://..." and "android:apk-key-hash:..."
    bool require_user_verification = true; // PIN or biometrics, not just a tap
};

struct Registration {
    std::string credential_id; // raw bytes
    std::string public_key;    // 65 bytes: 0x04 || X || Y (uncompressed P-256 point)
    std::uint32_t sign_count = 0;
    bool backup_eligible = false; // a synced passkey (iCloud Keychain, Google Password Manager)
    bool backup_state = false;
    std::string aaguid; // 16 bytes
};

struct Assertion {
    std::uint32_t sign_count = 0;
    bool backup_eligible = false;
    bool backup_state = false;
};

// ── base64url (RFC 4648 section 5) without padding, the form WebAuthn uses ────

inline auto base64url_encode(std::string_view bytes) -> std::string {
    std::string out(sodium_base64_ENCODED_LEN(bytes.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): libsodium takes unsigned char*
    sodium_bin2base64(out.data(), out.size(), reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(),
                      sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    out.resize(out.find('\0'));
    return out;
}

// Strict: no padding, no whitespace, no characters outside the alphabet, no stray trailing bits.
inline auto base64url_decode(std::string_view text) -> std::optional<std::string> {
    std::string out((text.size() / 4 * 3) + 3, '\0');
    std::size_t len = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): libsodium takes unsigned char*
    if (sodium_base642bin(reinterpret_cast<unsigned char*>(out.data()), out.size(), text.data(), text.size(), nullptr,
                          &len, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
        return std::nullopt;
    }
    out.resize(len);
    return out;
}

// ── verification ──────────────────────────────────────────────────────────────

// `challenge` is the raw bytes you issued (at least 16). clientDataJSON and attestationObject are
// the raw bytes the authenticator produced, already base64url-decoded.
[[nodiscard]] auto verify_registration(const Config& cfg, std::string_view challenge, std::string_view client_data_json,
                                       std::string_view attestation_object) -> std::expected<Registration, Error>;

// `public_key` and `stored_sign_count` are what verify_registration returned (and the last assertion
// updated). Store the returned sign_count afterwards.
[[nodiscard]] auto verify_assertion(const Config& cfg, std::string_view challenge, std::string_view public_key,
                                    std::uint32_t stored_sign_count, std::string_view client_data_json,
                                    std::string_view authenticator_data,
                                    std::string_view signature) -> std::expected<Assertion, Error>;

} // namespace web::webauthn
