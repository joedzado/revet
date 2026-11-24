// A software authenticator for tests: makes the bytes a phone or security key would send for
// registration and login, with knobs to break each field. Uses OpenSSL directly (tests only) and its
// own tiny CBOR encoder, so it shares no code with the verifier under test.
#pragma once

#include <cstdint>
#include <memory>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <revet/webauthn.hpp>
#include <sodium.h>
#include <string>

// NOLINTBEGIN(bugprone-signed-bitwise,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-type-reinterpret-cast,modernize-avoid-c-arrays):

// test code: REQUIRE guards optionals and sizes, fixtures use C APIs, static vectors and skip macros

namespace soft {

inline auto sha256(const std::string& s) -> std::string {
    unsigned char out[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(out, reinterpret_cast<const unsigned char*>(s.data()), s.size());
    return {reinterpret_cast<char*>(out), sizeof(out)};
}

// ── CBOR encoding ────────────────────────────────────────────────────────────

inline auto head(unsigned major, std::uint64_t n) -> std::string {
    std::string out;
    const auto m = static_cast<unsigned char>(major << 5U);
    if (n < 24) {
        out += static_cast<char>(m | n);
    } else if (n < 256) {
        out += static_cast<char>(m | 24);
        out += static_cast<char>(n);
    } else if (n < 65536) {
        out += static_cast<char>(m | 25);
        out += static_cast<char>(n >> 8);
        out += static_cast<char>(n & 0xFF);
    } else {
        out += static_cast<char>(m | 26);
        for (int s = 24; s >= 0; s -= 8) {
            out += static_cast<char>((n >> s) & 0xFF);
        }
    }
    return out;
}
inline auto uint(std::uint64_t n) -> std::string {
    return head(0, n);
}
inline auto nint(std::int64_t n) -> std::string {
    return head(1, static_cast<std::uint64_t>(-1 - n));
}
inline auto bstr(const std::string& b) -> std::string {
    return head(2, b.size()) + b;
}
inline auto tstr(const std::string& s) -> std::string {
    return head(3, s.size()) + s;
}
inline auto map(std::initializer_list<std::pair<std::string, std::string>> kv) -> std::string {
    std::string out = head(5, kv.size());
    for (const auto& [k, v] : kv) {
        out += k + v;
    }
    return out;
}

// ── keys ─────────────────────────────────────────────────────────────────────

struct PkeyFree {
    void operator()(EVP_PKEY* p) const {
        EVP_PKEY_free(p);
    }
};

struct Key {
    std::unique_ptr<EVP_PKEY, PkeyFree> pkey;
    std::string point; // 0x04 || X || Y

    static auto generate() -> Key {
        Key k;
        k.pkey.reset(EVP_EC_gen("P-256"));
        unsigned char buf[65];
        std::size_t len = 0;
        EVP_PKEY_get_octet_string_param(k.pkey.get(), OSSL_PKEY_PARAM_PUB_KEY, buf, sizeof(buf), &len);
        k.point.assign(reinterpret_cast<char*>(buf), len);
        return k;
    }

    [[nodiscard]] auto sign(const std::string& message) const -> std::string {
        const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
        EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, pkey.get());
        std::size_t len = 0;
        EVP_DigestSign(ctx.get(), nullptr, &len, reinterpret_cast<const unsigned char*>(message.data()),
                       message.size());
        std::string sig(len, '\0');
        EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(sig.data()), &len,
                       reinterpret_cast<const unsigned char*>(message.data()), message.size());
        sig.resize(len);
        return sig;
    }
};

// ── the authenticator ────────────────────────────────────────────────────────

constexpr unsigned char UP = 0x01, UV = 0x04, BE = 0x08, BS = 0x10, AT = 0x40, ED = 0x80;

struct Registration {
    std::string client_data_json;
    std::string attestation_object;
};

struct Assertion {
    std::string client_data_json;
    std::string authenticator_data;
    std::string signature;
};

// Fields a test can bend. Defaults produce a correct message.
struct Tweaks {
    std::string type;           // clientData type; empty = the right one
    std::string origin;         // empty = the authenticator's origin
    std::string challenge_text; // override the challenge text in clientDataJSON (base64url)
    std::string rp_id;          // empty = the authenticator's RP ID (hashed into authData)
    int flags = -1;             // -1 = UP|UV (+AT on registration)
    int sign_count = -1;        // -1 = the authenticator's counter
    bool cross_origin = false;
    std::string fmt = "none";
    std::string extra_json;  // raw members appended to clientDataJSON, e.g. R"(,"x":1)"
    bool break_cose = false; // wrong algorithm in the COSE key
};

struct Authenticator {
    std::string rp_id = "app.example.com";
    std::string origin = "https://app.example.com";
    std::string credential_id = std::string(32, '\x11');
    Key key = Key::generate();
    std::uint32_t counter = 0;

    static auto be32(std::uint32_t v) -> std::string {
        return std::string{static_cast<char>(v >> 24), static_cast<char>((v >> 16) & 0xFF),
                           static_cast<char>((v >> 8) & 0xFF), static_cast<char>(v & 0xFF)};
    }

    [[nodiscard]] auto cose_key(bool broken = false) const -> std::string {
        return map({{uint(1), uint(2)},
                    {uint(3), broken ? nint(-257) : nint(-7)},
                    {nint(-1), uint(1)},
                    {nint(-2), bstr(key.point.substr(1, 32))},
                    {nint(-3), bstr(key.point.substr(33, 32))}});
    }

    [[nodiscard]] auto client_data(const std::string& challenge, const std::string& default_type,
                                   const Tweaks& t) const -> std::string {
        const auto text = t.challenge_text.empty() ? web::webauthn::base64url_encode(challenge) : t.challenge_text;
        return std::string(R"({"type":")") + (t.type.empty() ? default_type : t.type) + R"(","challenge":")" + text +
               R"(","origin":")" + (t.origin.empty() ? origin : t.origin) + R"(","crossOrigin":)" +
               (t.cross_origin ? "true" : "false") + t.extra_json + "}";
    }

    [[nodiscard]] auto registration(const std::string& challenge, const Tweaks& t = {}) const -> Registration {
        const int flags = t.flags >= 0 ? t.flags : (UP | UV | AT);
        const auto count = static_cast<std::uint32_t>(t.sign_count >= 0 ? t.sign_count : static_cast<int>(counter));
        std::string auth = sha256(t.rp_id.empty() ? rp_id : t.rp_id);
        auth += static_cast<char>(flags);
        auth += be32(count);
        if ((flags & AT) != 0) {
            auth += std::string(16, '\0');
            auth += std::string{static_cast<char>(credential_id.size() >> 8),
                                static_cast<char>(credential_id.size() & 0xFF)};
            auth += credential_id;
            auth += cose_key(t.break_cose);
        }
        return {.client_data_json = client_data(challenge, "webauthn.create", t),
                .attestation_object =
                    map({{tstr("fmt"), tstr(t.fmt)}, {tstr("attStmt"), map({})}, {tstr("authData"), bstr(auth)}})};
    }

    // Each call advances the counter, like a real authenticator, unless the test pins it.
    [[nodiscard]] auto assertion(const std::string& challenge, const Tweaks& t = {}) -> Assertion {
        const int flags = t.flags >= 0 ? t.flags : (UP | UV);
        const auto count = t.sign_count >= 0 ? static_cast<std::uint32_t>(t.sign_count) : ++counter;
        std::string auth = sha256(t.rp_id.empty() ? rp_id : t.rp_id);
        auth += static_cast<char>(flags);
        auth += be32(count);
        const auto client = client_data(challenge, "webauthn.get", t);
        return {.client_data_json = client, .authenticator_data = auth, .signature = key.sign(auth + sha256(client))};
    }
};

} // namespace soft
// NOLINTEND(bugprone-signed-bitwise,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-type-reinterpret-cast,modernize-avoid-c-arrays)
