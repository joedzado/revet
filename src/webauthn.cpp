// WebAuthn verification. The OpenSSL includes stay in this file so apps that never touch passkeys
// do not compile them into every translation unit.
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <revet/cbor.hpp>
#include <revet/crypto.hpp>
#include <revet/webauthn.hpp>
#include <simdjson.h>

namespace web::webauthn {
namespace {

constexpr std::size_t k_min_challenge = 16;
constexpr std::size_t k_max_credential_id = 1023; // WebAuthn level 2, 6.1
constexpr unsigned char k_flag_up = 0x01;
constexpr unsigned char k_flag_uv = 0x04;
constexpr unsigned char k_flag_be = 0x08;
constexpr unsigned char k_flag_bs = 0x10;
constexpr unsigned char k_flag_at = 0x40;
constexpr unsigned char k_flag_ed = 0x80;

using Sha256 = std::array<unsigned char, crypto_hash_sha256_BYTES>;

auto sha256(std::string_view data) -> Sha256 {
    web::crypto::init();
    Sha256 out{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): libsodium takes unsigned char*
    crypto_hash_sha256(out.data(), reinterpret_cast<const unsigned char*>(data.data()), data.size());
    return out;
}

auto as_view(const Sha256& h) -> std::string_view {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): byte view as chars
    return {reinterpret_cast<const char*>(h.data()), h.size()};
}

// ── clientDataJSON ───────────────────────────────────────────────────────────

auto check_client_data(const Config& cfg, std::string_view expected_type, std::string_view challenge,
                       std::string_view json) -> std::expected<Sha256, Error> {
    simdjson::dom::parser parser;
    const simdjson::padded_string padded{json};
    simdjson::dom::element doc;
    if (parser.parse(padded).get(doc) != simdjson::SUCCESS) {
        return std::unexpected(Error::bad_client_data);
    }
    simdjson::dom::object obj;
    if (doc.get_object().get(obj) != simdjson::SUCCESS) {
        return std::unexpected(Error::bad_client_data);
    }

    std::string_view type;
    std::string_view got_challenge;
    std::string_view origin;
    if (obj["type"].get_string().get(type) != simdjson::SUCCESS ||
        obj["challenge"].get_string().get(got_challenge) != simdjson::SUCCESS ||
        obj["origin"].get_string().get(origin) != simdjson::SUCCESS) {
        return std::unexpected(Error::bad_client_data);
    }
    if (type != expected_type) {
        return std::unexpected(Error::wrong_type);
    }

    // A challenge we could not trust to be unguessable must not pass, even if it matches.
    if (challenge.size() < k_min_challenge) {
        return std::unexpected(Error::wrong_challenge);
    }
    const auto decoded = base64url_decode(got_challenge);
    if (!decoded || !web::crypto::secure_eq(*decoded, challenge)) {
        return std::unexpected(Error::wrong_challenge);
    }

    if (!std::ranges::any_of(cfg.origins, [&](const std::string& o) -> bool { return o == origin; })) {
        return std::unexpected(Error::wrong_origin);
    }
    bool cross = false;
    if (obj["crossOrigin"].get_bool().get(cross) == simdjson::SUCCESS && cross) {
        return std::unexpected(Error::cross_origin);
    }

    return sha256(json);
}

// ── authenticator data ───────────────────────────────────────────────────────

struct Credential {
    std::string aaguid;
    std::string id;
    web::cbor::Value cose_key;
};

struct AuthData {
    std::string_view rp_id_hash;
    unsigned char flags = 0;
    std::uint32_t sign_count = 0;
    std::optional<Credential> credential;
};

// Callers check the length first; at() keeps a slip from reading past the end.
auto byte_at(std::string_view d, std::size_t i) -> unsigned char {
    return static_cast<unsigned char>(d.at(i));
}

auto parse_auth_data(std::string_view d) -> std::expected<AuthData, Error> {
    constexpr std::size_t fixed = 37; // rpIdHash 32, flags 1, signCount 4
    if (d.size() < fixed) {
        return std::unexpected(Error::bad_auth_data);
    }
    AuthData out;
    out.rp_id_hash = d.substr(0, 32);
    out.flags = byte_at(d, 32);
    out.sign_count = (static_cast<std::uint32_t>(byte_at(d, 33)) << 24U) |
                     (static_cast<std::uint32_t>(byte_at(d, 34)) << 16U) |
                     (static_cast<std::uint32_t>(byte_at(d, 35)) << 8U) | static_cast<std::uint32_t>(byte_at(d, 36));
    if ((out.flags & k_flag_bs) != 0 && (out.flags & k_flag_be) == 0) {
        return std::unexpected(Error::bad_backup_flags);
    }

    std::size_t pos = fixed;
    if ((out.flags & k_flag_at) != 0) {
        if (d.size() < pos + 16 + 2) {
            return std::unexpected(Error::bad_auth_data);
        }
        Credential c;
        c.aaguid = std::string(d.substr(pos, 16));
        pos += 16;
        const std::size_t id_len =
            (static_cast<std::size_t>(byte_at(d, pos)) << 8U) | static_cast<std::size_t>(byte_at(d, pos + 1));
        pos += 2;
        if (id_len == 0 || id_len > k_max_credential_id) {
            return std::unexpected(Error::bad_credential_data);
        }
        if (d.size() < pos + id_len) {
            return std::unexpected(Error::bad_auth_data);
        }
        c.id = std::string(d.substr(pos, id_len));
        pos += id_len;
        auto key = web::cbor::decode_prefix(d.substr(pos));
        if (!key) {
            return std::unexpected(Error::bad_auth_data);
        }
        c.cose_key = std::move(key->value);
        pos += key->consumed;
        out.credential = std::move(c);
    }
    if ((out.flags & k_flag_ed) != 0) {
        auto ext = web::cbor::decode_prefix(d.substr(pos));
        if (!ext || !ext->value.is_map()) {
            return std::unexpected(Error::bad_auth_data);
        }
        pos += ext->consumed;
    }
    if (pos != d.size()) {
        return std::unexpected(Error::bad_auth_data);
    }
    return out;
}

auto check_flags_and_rp(const Config& cfg, const AuthData& a) -> std::optional<Error> {
    const auto expected = sha256(cfg.rp_id);
    if (!web::crypto::secure_eq(a.rp_id_hash, as_view(expected))) {
        return Error::wrong_rp_id;
    }
    if ((a.flags & k_flag_up) == 0) {
        return Error::user_not_present;
    }
    if (cfg.require_user_verification && (a.flags & k_flag_uv) == 0) {
        return Error::user_not_verified;
    }
    return std::nullopt;
}

// ── keys and signatures ──────────────────────────────────────────────────────

struct PkeyDeleter {
    void operator()(EVP_PKEY* p) const {
        EVP_PKEY_free(p);
    }
};
struct PkeyCtxDeleter {
    void operator()(EVP_PKEY_CTX* p) const {
        EVP_PKEY_CTX_free(p);
    }
};
struct MdCtxDeleter {
    void operator()(EVP_MD_CTX* p) const {
        EVP_MD_CTX_free(p);
    }
};
struct BldDeleter {
    void operator()(OSSL_PARAM_BLD* p) const {
        OSSL_PARAM_BLD_free(p);
    }
};
struct ParamDeleter {
    void operator()(OSSL_PARAM* p) const {
        OSSL_PARAM_free(p);
    }
};

using Pkey = std::unique_ptr<EVP_PKEY, PkeyDeleter>;

// `point` is 0x04 || X || Y. OpenSSL rejects a point that is not on the curve.
auto load_public_key(std::string_view point) -> Pkey {
    if (point.size() != 65 || point.front() != 0x04) {
        return nullptr;
    }
    const std::unique_ptr<OSSL_PARAM_BLD, BldDeleter> bld(OSSL_PARAM_BLD_new());
    if (!bld) {
        return nullptr;
    }
    if (OSSL_PARAM_BLD_push_utf8_string(bld.get(), OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0) != 1 ||
        OSSL_PARAM_BLD_push_octet_string(bld.get(), OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size()) != 1) {
        return nullptr;
    }
    const std::unique_ptr<OSSL_PARAM, ParamDeleter> params(OSSL_PARAM_BLD_to_param(bld.get()));
    const std::unique_ptr<EVP_PKEY_CTX, PkeyCtxDeleter> ctx(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
    if (!params || !ctx || EVP_PKEY_fromdata_init(ctx.get()) != 1) {
        return nullptr;
    }
    EVP_PKEY* raw = nullptr;
    if (EVP_PKEY_fromdata(ctx.get(), &raw, EVP_PKEY_PUBLIC_KEY, params.get()) != 1) {
        return nullptr;
    }
    Pkey key(raw);
    const std::unique_ptr<EVP_PKEY_CTX, PkeyCtxDeleter> check(EVP_PKEY_CTX_new_from_pkey(nullptr, key.get(), nullptr));
    if (!check || EVP_PKEY_public_check(check.get()) != 1) {
        return nullptr;
    }
    return key;
}

// RFC 9053 COSE_Key for ES256: {1: 2 (EC2), 3: -7 (ES256), -1: 1 (P-256), -2: x, -3: y}
auto point_from_cose(const web::cbor::Value& key) -> std::expected<std::string, Error> {
    if (!key.is_map()) {
        return std::unexpected(Error::unsupported_algorithm);
    }
    const auto int_is = [&](std::int64_t label, std::int64_t want) -> bool {
        const auto* v = key.find(label);
        const auto* i = v != nullptr ? v->as_int() : nullptr;
        return i != nullptr && *i == want;
    };
    if (!int_is(1, 2) || !int_is(3, -7) || !int_is(-1, 1)) {
        return std::unexpected(Error::unsupported_algorithm);
    }
    const auto* x = key.find(-2) != nullptr ? key.find(-2)->as_bytes() : nullptr;
    const auto* y = key.find(-3) != nullptr ? key.find(-3)->as_bytes() : nullptr;
    if (x == nullptr || y == nullptr || x->size() != 32 || y->size() != 32) {
        return std::unexpected(Error::bad_public_key);
    }
    std::string point(1, '\x04');
    point += *x;
    point += *y;
    return point;
}

auto verify_es256(EVP_PKEY* key, std::string_view message, std::string_view der_signature) -> bool {
    const std::unique_ptr<EVP_MD_CTX, MdCtxDeleter> ctx(EVP_MD_CTX_new());
    if (!ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key) != 1) {
        return false;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): OpenSSL takes unsigned char*
    return EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(der_signature.data()),
                            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): byte view as chars
                            der_signature.size(), reinterpret_cast<const unsigned char*>(message.data()),
                            message.size()) == 1;
}

} // namespace

auto verify_registration(const Config& cfg, std::string_view challenge, std::string_view client_data_json,
                         std::string_view attestation_object) -> std::expected<Registration, Error> {
    auto client = check_client_data(cfg, "webauthn.create", challenge, client_data_json);
    if (!client) {
        return std::unexpected(client.error());
    }

    auto att = web::cbor::decode(attestation_object);
    if (!att || !att->is_map()) {
        return std::unexpected(Error::bad_attestation);
    }
    const auto* fmt = att->find("fmt");
    const auto* stmt = att->find("attStmt");
    const auto* auth = att->find("authData");
    if (fmt == nullptr || !fmt->is_text() || stmt == nullptr || !stmt->is_map() || auth == nullptr ||
        !auth->is_bytes()) {
        return std::unexpected(Error::bad_attestation);
    }

    auto data = parse_auth_data(*auth->as_bytes());
    if (!data) {
        return std::unexpected(data.error());
    }
    if (const auto bad = check_flags_and_rp(cfg, *data)) {
        return std::unexpected(*bad);
    }
    if (!data->credential) {
        return std::unexpected(Error::bad_credential_data);
    }

    auto point = point_from_cose(data->credential->cose_key);
    if (!point) {
        return std::unexpected(point.error());
    }
    if (!load_public_key(*point)) {
        return std::unexpected(Error::bad_public_key);
    }

    return Registration{.credential_id = std::move(data->credential->id),
                        .public_key = std::move(*point),
                        .sign_count = data->sign_count,
                        .backup_eligible = (data->flags & k_flag_be) != 0,
                        .backup_state = (data->flags & k_flag_bs) != 0,
                        .aaguid = std::move(data->credential->aaguid)};
}

auto verify_assertion(const Config& cfg, std::string_view challenge, std::string_view public_key,
                      std::uint32_t stored_sign_count, std::string_view client_data_json,
                      std::string_view authenticator_data,
                      std::string_view signature) -> std::expected<Assertion, Error> {
    auto client = check_client_data(cfg, "webauthn.get", challenge, client_data_json);
    if (!client) {
        return std::unexpected(client.error());
    }

    auto data = parse_auth_data(authenticator_data);
    if (!data) {
        return std::unexpected(data.error());
    }
    if (const auto bad = check_flags_and_rp(cfg, *data)) {
        return std::unexpected(*bad);
    }

    const auto key = load_public_key(public_key);
    if (!key) {
        return std::unexpected(Error::bad_public_key);
    }

    std::string message(authenticator_data);
    message.append(as_view(*client));
    if (!verify_es256(key.get(), message, signature)) {
        return std::unexpected(Error::bad_signature);
    }

    // Counters that never move (synced passkeys report 0) are fine. Otherwise it must go up.
    if ((data->sign_count != 0 || stored_sign_count != 0) && data->sign_count <= stored_sign_count) {
        return std::unexpected(Error::sign_count_regressed);
    }

    return Assertion{.sign_count = data->sign_count,
                     .backup_eligible = (data->flags & k_flag_be) != 0,
                     .backup_state = (data->flags & k_flag_bs) != 0};
}

} // namespace web::webauthn
