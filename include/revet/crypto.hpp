#pragma once
#include <array>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sodium.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "c_api.hpp"
#include "log.hpp"

namespace web::crypto {

inline void init() {
    if (sodium_init() == -1) {
        throw std::runtime_error("Failed to initialize libsodium");
    }
}

// Constant-time equality. Use for any comparison of secrets, tokens, MACs, or
// cookie values where a timing oracle would leak information. NEVER use `==`
// on a secret. Strings of different lengths are not equal but the function
// still inspects both fully to avoid leaking lengths via early-exit timing.
inline auto secure_eq(std::string_view a, std::string_view b) -> bool {
    // sodium_memcmp returns 0 iff the byte ranges are equal AND have the
    // same length. Use a dummy comparison against `a` itself when lengths
    // differ so the work performed depends only on `a.size()`, not on `b`.
    const std::size_t len = a.size();
    const auto* lhs = c_api::uchars(a);
    const auto* rhs = (b.size() == len) ? c_api::uchars(b) : lhs;
    const bool same_bytes = sodium_memcmp(lhs, rhs, len) == 0;
    return same_bytes && a.size() == b.size();
}

// A hostname is guessable, so production should refuse to derive a key from one.
inline auto key_required() -> bool {
    const char* required = std::getenv("REVET_REQUIRE_COOKIE_KEY");
    return required != nullptr && std::string_view(required) == "1";
}

using Key = std::array<unsigned char, crypto_secretbox_KEYBYTES>;

// Fills key from REVET_COOKIE_KEY env var, or derives from hostname as a fallback.
inline void get_key(Key& key) {
    const char* key_env = std::getenv("REVET_COOKIE_KEY");
    if (key_env) {
        if (std::strlen(key_env) != 64) {
            throw std::runtime_error("REVET_COOKIE_KEY must be 64 hex characters");
        }
        size_t bin_len = 0;
        // Without this check, non-hex text silently gives a mostly empty key.
        if (sodium_hex2bin(key.data(), key.size(), key_env, 64, nullptr, &bin_len, nullptr) != 0 ||
            bin_len != crypto_secretbox_KEYBYTES) {
            throw std::runtime_error("REVET_COOKIE_KEY must be 64 hex characters");
        }
    } else if (key_required()) {
        throw std::runtime_error(
            "REVET_COOKIE_KEY is not set, and REVET_REQUIRE_COOKIE_KEY=1 forbids the hostname fallback");
    } else {
        // Cache the derived key — crypto_pwhash is intentionally slow
        static std::once_flag derived_once;
        static Key derived{};
        std::call_once(derived_once, []() -> void {
            log::warn("REVET_COOKIE_KEY is not set — deriving key from hostname. "
                      "Set REVET_COOKIE_KEY to a 64-char hex string for production use.");
            std::array<char, 256> hostname{};
            gethostname(hostname.data(), hostname.size() - 1);
            std::array<unsigned char, crypto_pwhash_SALTBYTES> salt{};
            const size_t hlen = std::strlen(hostname.data());
            crypto_generichash(salt.data(), salt.size(), c_api::uchars(hostname), hlen, nullptr, 0);
            if (crypto_pwhash(derived.data(), derived.size(), hostname.data(), hlen, salt.data(),
                              crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                              crypto_pwhash_ALG_DEFAULT) != 0) {
                throw std::runtime_error("Failed to derive key from hostname");
            }
        });
        key = derived;
    }
}

// Encrypt plaintext with XChaCha20-Poly1305
// Returns: base64(nonce || ciphertext)
inline auto encrypt(std::string_view plaintext) -> std::string {
    static thread_local bool initialized = false;
    if (!initialized) {
        init();
        initialized = true;
    }

    Key key{};
    get_key(key);

    // Generate random nonce
    std::array<unsigned char, crypto_secretbox_NONCEBYTES> nonce{};
    randombytes_buf(nonce.data(), nonce.size());

    // Encrypt
    std::string ciphertext(plaintext.size() + crypto_secretbox_MACBYTES, '\0');
    crypto_secretbox_easy(c_api::uchars(ciphertext), c_api::uchars(plaintext), plaintext.size(), nonce.data(),
                          key.data());

    // Combine nonce + ciphertext + base64 encode
    std::string combined(nonce.begin(), nonce.end());
    combined.append(ciphertext);

    std::vector<char> b64(combined.size() * 2 + 1);
    sodium_bin2base64(b64.data(), b64.size(), c_api::uchars(combined), combined.size(), sodium_base64_VARIANT_ORIGINAL);

    return {b64.data()};
}

// Decrypt base64(nonce || ciphertext) -> plaintext
inline auto decrypt(std::string_view encrypted) -> std::string {
    static thread_local bool initialized = false;
    if (!initialized) {
        init();
        initialized = true;
    }

    Key key{};
    get_key(key);

    // Base64 decode
    std::string combined(encrypted.size(), '\0');
    size_t combined_len;
    int ret = sodium_base642bin(c_api::uchars(combined), combined.size(), encrypted.data(), encrypted.size(), nullptr,
                                &combined_len, nullptr, sodium_base64_VARIANT_ORIGINAL);
    if (ret != 0 || combined_len < crypto_secretbox_NONCEBYTES) {
        return ""; // Decryption failed
    }

    // Extract nonce and ciphertext
    std::array<unsigned char, crypto_secretbox_NONCEBYTES> nonce{};
    std::memcpy(nonce.data(), combined.data(), nonce.size());

    const unsigned char* ciphertext = c_api::uchars(std::string_view(combined).substr(nonce.size()));
    const size_t ciphertext_len = combined_len - nonce.size();

    if (ciphertext_len < crypto_secretbox_MACBYTES) {
        return ""; // Too short
    }

    // Decrypt
    std::string plaintext(ciphertext_len - crypto_secretbox_MACBYTES, '\0');
    ret = crypto_secretbox_open_easy(c_api::uchars(plaintext), ciphertext, ciphertext_len, nonce.data(), key.data());

    if (ret != 0) {
        log::warn("crypto::decrypt: authentication failed (bad key or corrupted ciphertext)");
        return "";
    }

    return plaintext;
}

} // namespace web::crypto
