#pragma once
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sodium.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

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
inline bool secure_eq(std::string_view a, std::string_view b) {
    // sodium_memcmp returns 0 iff the byte ranges are equal AND have the
    // same length. Use a dummy comparison against `a` itself when lengths
    // differ so the work performed depends only on `a.size()`, not on `b`.
    const std::size_t len = a.size();
    const auto*       lhs = reinterpret_cast<const unsigned char*>(a.data());
    const auto*       rhs = (b.size() == len)
                                ? reinterpret_cast<const unsigned char*>(b.data())
                                : lhs;
    const bool same_bytes  = sodium_memcmp(lhs, rhs, len) == 0;
    return same_bytes && a.size() == b.size();
}

// Fills key from REVET_COOKIE_KEY env var, or derives from hostname as a fallback.
inline void get_key(unsigned char key[crypto_secretbox_KEYBYTES]) {
    const char* key_env = std::getenv("REVET_COOKIE_KEY");
    if (key_env) {
        if (std::strlen(key_env) != 64) {
            throw std::runtime_error("REVET_COOKIE_KEY must be 64 hex characters");
        }
        size_t bin_len;
        sodium_hex2bin(key, crypto_secretbox_KEYBYTES, key_env, 64, nullptr, &bin_len, nullptr);
    } else {
        // Cache the derived key — crypto_pwhash is intentionally slow
        static std::once_flag derived_once;
        static unsigned char derived[crypto_secretbox_KEYBYTES];
        std::call_once(derived_once, []() {
            log::warn("REVET_COOKIE_KEY is not set — deriving key from hostname. "
                      "Set REVET_COOKIE_KEY to a 64-char hex string for production use.");
            char hostname[256];
            hostname[sizeof(hostname) - 1] = '\0';
            gethostname(hostname, sizeof(hostname) - 1);
            unsigned char salt[crypto_pwhash_SALTBYTES];
            const size_t hlen = std::strlen(hostname);
            crypto_generichash(salt, crypto_pwhash_SALTBYTES, reinterpret_cast<const unsigned char*>(hostname), hlen,
                               nullptr, 0);
            if (crypto_pwhash(derived, crypto_secretbox_KEYBYTES, hostname, hlen, salt,
                              crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                              crypto_pwhash_ALG_DEFAULT) != 0) {
                throw std::runtime_error("Failed to derive key from hostname");
            }
        });
        std::memcpy(key, derived, crypto_secretbox_KEYBYTES);
    }
}

// Encrypt plaintext with XChaCha20-Poly1305
// Returns: base64(nonce || ciphertext)
inline std::string encrypt(std::string_view plaintext) {
    static thread_local bool initialized = false;
    if (!initialized) {
        init();
        initialized = true;
    }

    unsigned char key[crypto_secretbox_KEYBYTES];
    get_key(key);

    // Generate random nonce
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    // Encrypt
    std::string ciphertext(plaintext.size() + crypto_secretbox_MACBYTES, '\0');
    crypto_secretbox_easy(reinterpret_cast<unsigned char*>(ciphertext.data()),
                          reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(), nonce, key);

    // Combine nonce + ciphertext + base64 encode
    std::string combined(nonce, nonce + sizeof(nonce));
    combined.append(ciphertext);

    std::vector<char> b64(combined.size() * 2 + 1);
    sodium_bin2base64(b64.data(), b64.size(), reinterpret_cast<const unsigned char*>(combined.data()), combined.size(),
                      sodium_base64_VARIANT_ORIGINAL);

    return std::string(b64.data());
}

// Decrypt base64(nonce || ciphertext) -> plaintext
inline std::string decrypt(std::string_view encrypted) {
    static thread_local bool initialized = false;
    if (!initialized) {
        init();
        initialized = true;
    }

    unsigned char key[crypto_secretbox_KEYBYTES];
    get_key(key);

    // Base64 decode
    std::string combined(encrypted.size(), '\0');
    size_t combined_len;
    int ret = sodium_base642bin(reinterpret_cast<unsigned char*>(combined.data()), combined.size(), encrypted.data(),
                                encrypted.size(), nullptr, &combined_len, nullptr, sodium_base64_VARIANT_ORIGINAL);
    if (ret != 0 || combined_len < crypto_secretbox_NONCEBYTES) {
        return ""; // Decryption failed
    }

    // Extract nonce and ciphertext
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    std::memcpy(nonce, combined.data(), sizeof(nonce));

    const unsigned char* ciphertext = reinterpret_cast<const unsigned char*>(combined.data()) + sizeof(nonce);
    size_t ciphertext_len = combined_len - sizeof(nonce);

    if (ciphertext_len < crypto_secretbox_MACBYTES) {
        return ""; // Too short
    }

    // Decrypt
    std::string plaintext(ciphertext_len - crypto_secretbox_MACBYTES, '\0');
    ret = crypto_secretbox_open_easy(reinterpret_cast<unsigned char*>(plaintext.data()), ciphertext, ciphertext_len,
                                     nonce, key);

    if (ret != 0) {
        log::warn("crypto::decrypt: authentication failed (bad key or corrupted ciphertext)");
        return "";
    }

    return plaintext;
}

} // namespace web::crypto
