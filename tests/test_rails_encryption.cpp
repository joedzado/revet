// Checks against vectors produced by real Rails (ActiveRecord::Encryption, from
// Rails 8.1) with throwaway keys. Fixture: tests/fixtures/rails_encryption_vectors.json
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <revet/rails_encryption.hpp>
#include <simdjson.h>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using rails::Encryption;
using rails::EncryptionError;
using rails::Scheme;

namespace {

struct Vector {
    std::string name;
    std::string plain;
    std::string cipher;
};

struct Fixture {
    rails::EncryptionConfig config;
    std::string primary_derived_hex;
    std::string deterministic_derived_hex;
    std::vector<Vector> primary;
    std::vector<Vector> deterministic;
};

auto read_fixture_text() -> std::string {
    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "rails_encryption_vectors.json";
    const std::ifstream in(path);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

auto str(simdjson::dom::element e, const char* key) -> std::string {
    return std::string(std::string_view(e[key]));
}

auto vectors(simdjson::dom::element root, const char* key) -> std::vector<Vector> {
    std::vector<Vector> out;
    for (const simdjson::dom::element v : simdjson::dom::array(root[key])) {
        out.push_back({.name = str(v, "name"), .plain = str(v, "plain"), .cipher = str(v, "cipher")});
    }
    return out;
}

auto fixture() -> const Fixture& {
    static const Fixture f = [] -> Fixture {
        const auto text = read_fixture_text();
        simdjson::dom::parser parser;
        const simdjson::padded_string padded(text);
        const simdjson::dom::element root = parser.parse(padded);
        const simdjson::dom::element keys = root["keys"];
        Fixture x;
        x.config = {.primary_key = str(keys, "primary"),
                    .deterministic_key = str(keys, "deterministic"),
                    .key_derivation_salt = str(keys, "salt")};
        x.primary_derived_hex = str(keys, "primary_derived_hex");
        x.deterministic_derived_hex = str(keys, "deterministic_derived_hex");
        x.primary = vectors(root, "primary");
        x.deterministic = vectors(root, "deterministic");
        return x;
    }();
    return f;
}

auto hex(const std::array<unsigned char, Encryption::kKeyBytes>& key) -> std::string {
    return std::ranges::fold_left(key, std::string(), [](std::string acc, unsigned char c) -> std::string {
        acc += std::format("{:02x}", c);
        return acc;
    });
}

// Flips one payload character, keeping the base64 valid.
auto tamper(std::string message) -> std::string {
    const auto start = message.find(R"("p":")") + 5;
    message.at(start) = message.at(start) == 'A' ? 'B' : 'A';
    return message;
}

auto by_name(const std::vector<Vector>& all, const std::string& name) -> const Vector& {
    for (const auto& v : all) {
        if (v.name == name) {
            return v;
        }
    }
    FAIL("no vector named " << name);
    return all.front();
}

} // namespace

TEST_CASE("rails encryption: key derivation matches Rails", "[rails]") {
    const auto& f = fixture();
    CHECK(hex(Encryption::derive_key(f.config.primary_key, f.config.key_derivation_salt)) == f.primary_derived_hex);
    CHECK(hex(Encryption::derive_key(f.config.deterministic_key, f.config.key_derivation_salt)) ==
          f.deterministic_derived_hex);
}

TEST_CASE("rails encryption: decrypts every Rails primary vector", "[rails]") {
    const auto& f = fixture();
    const Encryption enc(f.config);
    REQUIRE_FALSE(f.primary.empty());
    for (const auto& v : f.primary) {
        INFO(v.name);
        CHECK(enc.decrypt(v.cipher, Scheme::Primary) == v.plain);
    }
}

TEST_CASE("rails encryption: decrypts every Rails deterministic vector", "[rails]") {
    const auto& f = fixture();
    const Encryption enc(f.config);
    REQUIRE_FALSE(f.deterministic.empty());
    for (const auto& v : f.deterministic) {
        INFO(v.name);
        CHECK(enc.decrypt(v.cipher, Scheme::Deterministic) == v.plain);
    }
}

TEST_CASE("rails encryption: deterministic output is byte-identical to Rails", "[rails]") {
    const auto& f = fixture();
    const Encryption enc(f.config);
    for (const auto& v : f.deterministic) {
        INFO(v.name);
        CHECK(enc.encrypt(v.plain, Scheme::Deterministic) == v.cipher);
    }
}

TEST_CASE("rails encryption: primary round-trips with a fresh IV each time", "[rails]") {
    const auto& f = fixture();
    const Encryption enc(f.config);
    for (const auto& v : f.primary) {
        INFO(v.name);
        const auto a = enc.encrypt(v.plain, Scheme::Primary);
        const auto b = enc.encrypt(v.plain, Scheme::Primary);
        CHECK(a != b);
        CHECK(enc.decrypt(a, Scheme::Primary) == v.plain);
    }
}

TEST_CASE("rails encryption: compression follows the Rails threshold", "[rails]") {
    const auto& f = fixture();
    const Encryption enc(f.config);
    CHECK(!enc.encrypt(std::string(140, 'a'), Scheme::Primary).contains(R"("c":true)"));
    CHECK(enc.encrypt(std::string(141, 'a'), Scheme::Primary).contains(R"("c":true)"));
}

TEST_CASE("rails encryption: reads jsonb shapes", "[rails]") {
    const auto& f = fixture();
    const Encryption enc(f.config);
    const auto& v = by_name(f.primary, "json");

    // A jsonb string scalar: the message, JSON-escaped and quoted.
    std::string quoted =
        std::ranges::fold_left(v.cipher, std::string("\""), [](std::string acc, char c) -> std::string {
            if (c == '"' || c == '\\') {
                acc += '\\';
            }
            acc += c;
            return acc;
        });
    quoted += '"';
    CHECK(enc.decrypt(quoted, Scheme::Primary) == v.plain);

    // A jsonb object: Postgres reorders keys and adds spaces.
    const auto p = v.cipher.find(R"("h":)");
    const auto end = v.cipher.size() - 1;
    const auto payload = v.cipher.substr(1, p - 2);
    const auto reordered = "{" + v.cipher.substr(p, end - p) + ", " + payload + "}";
    CHECK(enc.decrypt(reordered, Scheme::Primary) == v.plain);
}

TEST_CASE("rails encryption: rejects tampering, wrong keys and wrong scheme", "[rails]") {
    const auto& f = fixture();
    const Encryption enc(f.config);
    const auto& v = by_name(f.primary, "short");

    CHECK_THROWS_AS(enc.decrypt(tamper(v.cipher), Scheme::Primary), EncryptionError);
    CHECK_THROWS_AS(enc.decrypt(v.cipher, Scheme::Deterministic), EncryptionError);

    auto other = f.config;
    other.primary_key = "a different key";
    const Encryption wrong(other);
    CHECK_THROWS_AS(wrong.decrypt(v.cipher, Scheme::Primary), EncryptionError);
}

TEST_CASE("rails encryption: rejects malformed messages", "[rails]") {
    const auto& f = fixture();
    const Encryption enc(f.config);
    CHECK_THROWS_AS(enc.decrypt("plain text", Scheme::Primary), EncryptionError);
    CHECK_THROWS_AS(enc.decrypt(R"({"p":"AAAA"})", Scheme::Primary), EncryptionError);
    CHECK_THROWS_AS(enc.decrypt(R"({"p":"!!","h":{"iv":"AAAA","at":"AAAA"}})", Scheme::Primary), EncryptionError);
    CHECK_THROWS_AS(enc.decrypt(R"({"p":"AAAA","h":{"iv":"AAAA","at":"AAAA"}})", Scheme::Primary), EncryptionError);
    CHECK_THROWS_AS(enc.decrypt(R"({"p":"AAAA","h":{"iv":"AAAA","at":"AAAA","k":{"p":"AAAA"}}})", Scheme::Primary),
                    EncryptionError);
    CHECK_THROWS_AS(enc.decrypt("", Scheme::Primary), EncryptionError);
}

TEST_CASE("rails encryption: errors never contain the input", "[rails]") {
    const auto& f = fixture();
    const Encryption enc(f.config);
    const std::string secret = "topsecretvalue";
    try {
        (void)enc.decrypt(secret, Scheme::Primary);
        FAIL("expected a throw");
    } catch (const EncryptionError& e) {
        CHECK(!std::string(e.what()).contains(secret));
    }
}

TEST_CASE("rails encryption: is_encrypted", "[rails]") {
    const auto& f = fixture();
    CHECK(Encryption::is_encrypted(by_name(f.primary, "short").cipher));
    CHECK_FALSE(Encryption::is_encrypted("plain text"));
    CHECK_FALSE(Encryption::is_encrypted(R"({"id":"ACT-1"})"));
    CHECK_FALSE(Encryption::is_encrypted(""));
}

TEST_CASE("rails encryption: missing keys throw", "[rails]") {
    CHECK_THROWS_AS(Encryption(rails::EncryptionConfig{}), EncryptionError);
}
