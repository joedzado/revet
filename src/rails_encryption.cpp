#include <revet/rails_encryption.hpp>

#include <algorithm>
#include <optional>
#include <simdjson.h>
#include <sodium.h>
#include <zlib.h>

namespace rails {
namespace {

constexpr int kPbkdf2Rounds = 1 << 16;      // ActiveSupport::KeyGenerator default
constexpr std::size_t kCompressAbove = 140; // Encryptor::THRESHOLD_TO_JUSTIFY_COMPRESSION
constexpr std::size_t kMaxInflated = 256UL * 1024 * 1024;

const unsigned char* bytes(std::string_view s) {
    return reinterpret_cast<const unsigned char*>(s.data());
}

std::string base64_encode(std::string_view in) {
    std::string out(sodium_base64_ENCODED_LEN(in.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(out.data(), out.size(), bytes(in), in.size(), sodium_base64_VARIANT_ORIGINAL);
    out.resize(out.size() - 1); // drop the terminating NUL
    return out;
}

std::string base64_decode(std::string_view in) {
    std::string out(in.size() / 4 * 3 + 3, '\0');
    std::size_t len = 0;
    const char* end = nullptr;
    if (sodium_base642bin(reinterpret_cast<unsigned char*>(out.data()), out.size(), in.data(), in.size(), nullptr,
                          &len, &end, sodium_base64_VARIANT_ORIGINAL) != 0 ||
        end != in.data() + in.size()) {
        throw EncryptionError("rails encryption: invalid base64");
    }
    out.resize(len);
    return out;
}

std::string deflate_text(std::string_view in) {
    uLongf len = compressBound(static_cast<uLong>(in.size()));
    std::string out(len, '\0');
    if (compress(reinterpret_cast<Bytef*>(out.data()), &len, bytes(in), static_cast<uLong>(in.size())) != Z_OK) {
        throw EncryptionError("rails encryption: compress failed");
    }
    out.resize(len);
    return out;
}

std::string inflate_text(std::string_view in) {
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) {
        throw EncryptionError("rails encryption: inflate init failed");
    }
    zs.next_in = const_cast<Bytef*>(bytes(in));
    zs.avail_in = static_cast<uInt>(in.size());

    std::string out;
    std::array<char, 64 * 1024> buf{};
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        zs.next_out = reinterpret_cast<Bytef*>(buf.data());
        zs.avail_out = static_cast<uInt>(buf.size());
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&zs);
            throw EncryptionError("rails encryption: inflate failed");
        }
        out.append(buf.data(), buf.size() - zs.avail_out);
        if (out.size() > kMaxInflated) {
            inflateEnd(&zs);
            throw EncryptionError("rails encryption: inflated text too large");
        }
    }
    inflateEnd(&zs);
    return out;
}

struct Message {
    std::string payload;
    std::string iv;
    std::string auth_tag;
    bool compressed = false;
};

std::optional<std::string_view> string_field(simdjson::dom::object obj, const char* key) {
    std::string_view value;
    if (obj[key].get(value) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    return value;
}

// A jsonb column can hand back the message wrapped in a JSON string.
std::optional<std::string> unwrap_json_string(std::string_view value) {
    simdjson::dom::parser parser;
    const simdjson::padded_string padded(value);
    std::string_view inner;
    if (parser.parse(padded).get(inner) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    return std::string(inner);
}

std::optional<Message> parse_message(std::string_view value) {
    simdjson::dom::parser parser;
    const simdjson::padded_string padded(value);
    simdjson::dom::object root;
    if (parser.parse(padded).get(root) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    const auto payload = string_field(root, "p");
    simdjson::dom::object headers;
    if (!payload || root["h"].get(headers) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    const auto iv = string_field(headers, "iv");
    const auto tag = string_field(headers, "at");
    if (!iv || !tag) {
        return std::nullopt;
    }
    // "k" is a per-message data key, used only with envelope encryption.
    simdjson::dom::element ignored;
    if (headers["k"].get(ignored) == simdjson::SUCCESS) {
        throw EncryptionError("rails encryption: envelope encryption is not supported");
    }

    Message m;
    m.payload = base64_decode(*payload);
    m.iv = base64_decode(*iv);
    m.auth_tag = base64_decode(*tag);
    bool compressed = false;
    if (headers["c"].get(compressed) == simdjson::SUCCESS) {
        m.compressed = compressed;
    }
    return m;
}

std::optional<Message> parse_any(std::string_view value) {
    if (auto m = parse_message(value)) {
        return m;
    }
    if (const auto inner = unwrap_json_string(value)) {
        return parse_message(*inner);
    }
    return std::nullopt;
}

void require_sodium() {
    if (sodium_init() < 0) {
        throw EncryptionError("rails encryption: libsodium init failed");
    }
    if (crypto_aead_aes256gcm_is_available() == 0) {
        throw EncryptionError("rails encryption: this CPU has no hardware AES");
    }
}

} // namespace

std::array<unsigned char, Encryption::kKeyBytes> Encryption::derive_key(std::string_view password,
                                                                         std::string_view salt) {
    if (sodium_init() < 0) {
        throw EncryptionError("rails encryption: libsodium init failed");
    }
    static_assert(kKeyBytes == crypto_auth_hmacsha256_BYTES, "one PBKDF2 block yields exactly one key");

    const auto hmac = [&](std::string_view message, unsigned char* out) {
        crypto_auth_hmacsha256_state state;
        crypto_auth_hmacsha256_init(&state, bytes(password), password.size());
        crypto_auth_hmacsha256_update(&state, bytes(message), message.size());
        crypto_auth_hmacsha256_final(&state, out);
        sodium_memzero(&state, sizeof state);
    };

    // PBKDF2 block 1: U1 = HMAC(password, salt || INT32_BE(1)); Un = HMAC(password, Un-1).
    std::string first(salt);
    first.append({'\0', '\0', '\0', '\1'});

    std::array<unsigned char, kKeyBytes> u{};
    hmac(first, u.data());
    std::array<unsigned char, kKeyBytes> key = u;
    for (int round = 1; round < kPbkdf2Rounds; ++round) {
        const std::string_view previous(reinterpret_cast<const char*>(u.data()), u.size());
        std::array<unsigned char, kKeyBytes> next{};
        hmac(previous, next.data());
        u = next;
        std::transform(key.begin(), key.end(), u.begin(), key.begin(),
                       [](unsigned char a, unsigned char b) { return static_cast<unsigned char>(a ^ b); });
    }
    sodium_memzero(u.data(), u.size());
    return key;
}

Encryption::Encryption(const EncryptionConfig& config) {
    require_sodium();
    if (config.primary_key.empty() || config.deterministic_key.empty() || config.key_derivation_salt.empty()) {
        throw EncryptionError("rails encryption: missing key or salt");
    }
    primary_ = derive_key(config.primary_key, config.key_derivation_salt);
    deterministic_ = derive_key(config.deterministic_key, config.key_derivation_salt);
}

Encryption::~Encryption() {
    sodium_memzero(primary_.data(), primary_.size());
    sodium_memzero(deterministic_.data(), deterministic_.size());
}

const std::array<unsigned char, Encryption::kKeyBytes>& Encryption::key_for(Scheme scheme) const {
    return scheme == Scheme::Deterministic ? deterministic_ : primary_;
}

bool Encryption::is_encrypted(std::string_view value) {
    try {
        return parse_any(value).has_value();
    } catch (const EncryptionError&) {
        return false;
    }
}

std::string Encryption::decrypt(std::string_view message, Scheme scheme) const {
    const auto parsed = parse_any(message);
    if (!parsed) {
        throw EncryptionError("rails encryption: not an encrypted message");
    }
    if (parsed->iv.size() != crypto_aead_aes256gcm_NPUBBYTES || parsed->auth_tag.size() != crypto_aead_aes256gcm_ABYTES) {
        throw EncryptionError("rails encryption: bad IV or auth tag length");
    }

    std::string plain(parsed->payload.size(), '\0');
    if (crypto_aead_aes256gcm_decrypt_detached(reinterpret_cast<unsigned char*>(plain.data()), nullptr,
                                               bytes(parsed->payload), parsed->payload.size(), bytes(parsed->auth_tag),
                                               nullptr, 0, bytes(parsed->iv), key_for(scheme).data()) != 0) {
        throw EncryptionError("rails encryption: decryption failed");
    }
    return parsed->compressed ? inflate_text(plain) : plain;
}

std::string Encryption::encrypt(std::string_view plaintext, Scheme scheme) const {
    std::string text(plaintext);
    bool compressed = false;
    if (text.size() > kCompressAbove) {
        std::string deflated = deflate_text(text);
        if (deflated.size() < text.size()) {
            text = std::move(deflated);
            compressed = true;
        }
    }

    const auto& key = key_for(scheme);
    std::array<unsigned char, crypto_aead_aes256gcm_NPUBBYTES> iv{};
    if (scheme == Scheme::Deterministic) {
        // Rails derives this from the text after compression.
        std::array<unsigned char, crypto_auth_hmacsha256_BYTES> mac{};
        crypto_auth_hmacsha256_state state;
        crypto_auth_hmacsha256_init(&state, key.data(), key.size());
        crypto_auth_hmacsha256_update(&state, bytes(text), text.size());
        crypto_auth_hmacsha256_final(&state, mac.data());
        std::copy_n(mac.begin(), iv.size(), iv.begin());
        sodium_memzero(&state, sizeof state);
    } else {
        randombytes_buf(iv.data(), iv.size());
    }

    std::string cipher(text.size(), '\0');
    std::array<unsigned char, crypto_aead_aes256gcm_ABYTES> tag{};
    unsigned long long tag_len = 0;
    if (crypto_aead_aes256gcm_encrypt_detached(reinterpret_cast<unsigned char*>(cipher.data()), tag.data(), &tag_len,
                                               bytes(text), text.size(), nullptr, 0, nullptr, iv.data(),
                                               key.data()) != 0) {
        throw EncryptionError("rails encryption: encryption failed");
    }

    const auto view = [](const auto& a) {
        return std::string_view(reinterpret_cast<const char*>(a.data()), a.size());
    };
    // Base64 needs no JSON escaping. Field order matches Rails' JSON.dump.
    std::string out = R"({"p":")" + base64_encode(cipher) + R"(","h":{"iv":")" + base64_encode(view(iv)) +
                      R"(","at":")" + base64_encode(view(tag)) + '"';
    if (compressed) {
        out += R"(,"c":true)";
    }
    out += "}}";
    return out;
}

} // namespace rails
