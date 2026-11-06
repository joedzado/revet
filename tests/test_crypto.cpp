#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <revet/crypto.hpp>

TEST_CASE("crypto encrypt/decrypt round-trip with explicit key", "[crypto]") {
    setenv("REVET_COOKIE_KEY", "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20", 1);

    SECTION("basic string") {
        std::string ct = web::crypto::encrypt("hello world");
        REQUIRE(ct != "hello world");
        REQUIRE(web::crypto::decrypt(ct) == "hello world");
    }

    SECTION("empty string") {
        std::string ct = web::crypto::encrypt("");
        REQUIRE(web::crypto::decrypt(ct) == "");
    }

    SECTION("different nonces produce different ciphertext") {
        std::string ct1 = web::crypto::encrypt("same");
        std::string ct2 = web::crypto::encrypt("same");
        REQUIRE(ct1 != ct2);
        REQUIRE(web::crypto::decrypt(ct1) == "same");
        REQUIRE(web::crypto::decrypt(ct2) == "same");
    }

    SECTION("tampered ciphertext returns empty") {
        std::string ct = web::crypto::encrypt("secret");
        ct[ct.size() / 2] ^= 0xFF;
        REQUIRE(web::crypto::decrypt(ct) == "");
    }

    unsetenv("REVET_COOKIE_KEY");
}

TEST_CASE("crypto get_key rejects wrong-length key", "[crypto]") {
    setenv("REVET_COOKIE_KEY", "tooshort", 1);
    unsigned char buf[crypto_secretbox_KEYBYTES]{};
    REQUIRE_THROWS_AS(web::crypto::get_key(buf), std::runtime_error);
    unsetenv("REVET_COOKIE_KEY");
}

TEST_CASE("crypto hostname fallback still round-trips", "[crypto]") {
    unsetenv("REVET_COOKIE_KEY");
    std::string ct = web::crypto::encrypt("fallback test");
    REQUIRE(web::crypto::decrypt(ct) == "fallback test");
}

TEST_CASE("a cookie key must be real hex, and can be required", "[crypto]") {
    unsigned char key[crypto_secretbox_KEYBYTES];
    const char* saved = std::getenv("REVET_COOKIE_KEY");
    const std::string restore = saved != nullptr ? saved : "";

    setenv("REVET_COOKIE_KEY", std::string(64, 'z').c_str(), 1);
    CHECK_THROWS(web::crypto::get_key(key));
    setenv("REVET_COOKIE_KEY", std::string(64, 'a').c_str(), 1);
    CHECK_NOTHROW(web::crypto::get_key(key));

    unsetenv("REVET_COOKIE_KEY");
    setenv("REVET_REQUIRE_COOKIE_KEY", "1", 1);
    CHECK_THROWS(web::crypto::get_key(key));
    unsetenv("REVET_REQUIRE_COOKIE_KEY");
    CHECK_NOTHROW(web::crypto::get_key(key));

    if (saved != nullptr) {
        setenv("REVET_COOKIE_KEY", restore.c_str(), 1);
    }
}
