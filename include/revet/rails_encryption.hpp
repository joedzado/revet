#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

// Reads and writes columns encrypted by Rails' ActiveRecord::Encryption, using
// the default scheme: AES-256-GCM, keys from PBKDF2-HMAC-SHA256 (65536 rounds,
// no key references), zlib for text over 140 bytes.
//
// Output is checked byte for byte against real Rails output in
// tests/test_rails_encryption.cpp. Deterministic encryption has to match exactly,
// because Rails finds those rows by comparing ciphertext.
//
// AES-256-GCM comes from libsodium, which needs hardware AES. The constructor
// throws where it is missing, for example on a Raspberry Pi 4 or older.
namespace rails {

// Never contains plaintext, ciphertext or key material.
class EncryptionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Primary: random IV, for normal `encrypts :x` columns.
// Deterministic: IV derived from the text, for `encrypts :x, deterministic: true`.
enum class Scheme { Primary, Deterministic };

struct EncryptionConfig {
    std::string primary_key;         // ACTIVE_RECORD_ENCRYPTION_PRIMARY_KEY
    std::string deterministic_key;   // ACTIVE_RECORD_ENCRYPTION_DETERMINISTIC_KEY
    std::string key_derivation_salt; // ACTIVE_RECORD_ENCRYPTION_KEY_DERIVATION_SALT
};

class Encryption {
public:
    static constexpr std::size_t kKeyBytes = 32;

    explicit Encryption(const EncryptionConfig& config);
    ~Encryption();
    Encryption(const Encryption&) = delete;
    Encryption& operator=(const Encryption&) = delete;

    // Accepts the stored message as text, or as a jsonb column reads back: either
    // a JSON object, or a JSON string that holds one. Throws EncryptionError.
    std::string decrypt(std::string_view message, Scheme scheme) const;

    // Returns the message as Rails serializes it. Throws EncryptionError.
    std::string encrypt(std::string_view plaintext, Scheme scheme) const;

    // True if the value is shaped like an encrypted message. Does not check that
    // it decrypts. Useful where Rails allows unencrypted legacy values.
    static bool is_encrypted(std::string_view value);

    // PBKDF2-HMAC-SHA256 as ActiveSupport::KeyGenerator does it, for tests.
    static std::array<unsigned char, kKeyBytes> derive_key(std::string_view password, std::string_view salt);

private:
    const std::array<unsigned char, kKeyBytes>& key_for(Scheme scheme) const;

    std::array<unsigned char, kKeyBytes> primary_{};
    std::array<unsigned char, kKeyBytes> deterministic_{};
};

} // namespace rails
