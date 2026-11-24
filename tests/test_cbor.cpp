// Tests for web::cbor — the strict decoder behind WebAuthn parsing.
#include <catch2/catch_test_macros.hpp>
#include <revet/cbor.hpp>
#include <string>

using namespace std::string_literals;
namespace cbor = web::cbor;

namespace {
auto hex(std::string_view h) -> std::string {
    std::string out;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2) {
        out += static_cast<char>(std::stoi(std::string(h.substr(i, 2)), nullptr, 16));
    }
    return out;
}
auto int_of(const std::string& bytes) -> std::int64_t {
    const auto v = cbor::decode(bytes);
    REQUIRE(v.has_value());
    REQUIRE(v->is_int());
    return *v->as_int();
}
auto err_of(const std::string& bytes) -> cbor::Error {
    const auto v = cbor::decode(bytes);
    REQUIRE_FALSE(v.has_value());
    return v.error();
}
} // namespace

TEST_CASE("cbor: unsigned integers of every width", "[cbor]") {
    REQUIRE(int_of(hex("00")) == 0);
    REQUIRE(int_of(hex("17")) == 23);
    REQUIRE(int_of(hex("1818")) == 24);
    REQUIRE(int_of(hex("18ff")) == 255);
    REQUIRE(int_of(hex("190100")) == 256);
    REQUIRE(int_of(hex("19ffff")) == 65535);
    REQUIRE(int_of(hex("1a00010000")) == 65536);
    REQUIRE(int_of(hex("1affffffff")) == 4294967295LL);
    REQUIRE(int_of(hex("1b0000000100000000")) == 4294967296LL);
    REQUIRE(int_of(hex("1b7fffffffffffffff")) == INT64_MAX);
}

TEST_CASE("cbor: negative integers", "[cbor]") {
    REQUIRE(int_of(hex("20")) == -1);
    REQUIRE(int_of(hex("37")) == -24);
    REQUIRE(int_of(hex("3818")) == -25);
    REQUIRE(int_of(hex("390100")) == -257);
    REQUIRE(int_of(hex("3b7fffffffffffffff")) == INT64_MIN);
}

TEST_CASE("cbor: integers beyond int64 are refused", "[cbor]") {
    REQUIRE(err_of(hex("1b8000000000000000")) == cbor::Error::too_large);
    REQUIRE(err_of(hex("1bffffffffffffffff")) == cbor::Error::too_large);
    REQUIRE(err_of(hex("3b8000000000000000")) == cbor::Error::too_large);
}

TEST_CASE("cbor: strings", "[cbor]") {
    const auto b = cbor::decode(hex("43010203"));
    REQUIRE(b.has_value());
    REQUIRE(*b->as_bytes() == "\x01\x02\x03"s);
    REQUIRE_FALSE(b->is_text());

    const auto t = cbor::decode(hex("6568656c6c6f"));
    REQUIRE(t.has_value());
    REQUIRE(*t->as_text() == "hello");

    const auto empty = cbor::decode(hex("40"));
    REQUIRE(empty.has_value());
    REQUIRE(empty->as_bytes()->empty());
}

TEST_CASE("cbor: text must be valid UTF-8", "[cbor]") {
    REQUIRE(cbor::decode(hex("62c3a9")).has_value());            // é
    REQUIRE(err_of(hex("62c328")) == cbor::Error::bad_text);     // bad continuation
    REQUIRE(err_of(hex("61ff")) == cbor::Error::bad_text);       // never valid
    REQUIRE(err_of(hex("62c080")) == cbor::Error::bad_text);     // overlong
    REQUIRE(err_of(hex("63eda080")) == cbor::Error::bad_text);   // surrogate
    REQUIRE(err_of(hex("64f4908080")) == cbor::Error::bad_text); // above U+10FFFF
    REQUIRE(err_of(hex("62c3")) == cbor::Error::truncated);      // cut short
}

TEST_CASE("cbor: arrays, maps, booleans and null", "[cbor]") {
    const auto a = cbor::decode(hex("83010203"));
    REQUIRE(a.has_value());
    REQUIRE(a->as_array()->size() == 3);

    REQUIRE_FALSE(cbor::decode(hex("a2616101626262")).has_value()); // second pair has a key and no value

    const auto good = cbor::decode(hex("a2616101026179")); // {"a": 1, 2: "y"}
    REQUIRE(good.has_value());
    REQUIRE(*good->find("a")->as_int() == 1);
    REQUIRE(*good->find(2)->as_text() == "y");
    REQUIRE(good->find("zz") == nullptr);
    REQUIRE(good->find(3) == nullptr);
    REQUIRE(a->find("a") == nullptr); // not a map

    REQUIRE(cbor::decode(hex("f4")).has_value());
    REQUIRE(cbor::decode(hex("f5")).has_value());
    REQUIRE(cbor::decode(hex("f6")).has_value());
}

TEST_CASE("cbor: negative integer keys, as in a COSE key", "[cbor]") {
    // {1: 2, 3: -7, -1: 1, -2: h'0a', -3: h'0b'}
    const auto k = cbor::decode(hex("a501020326200121410a22410b"));
    REQUIRE(k.has_value());
    REQUIRE(*k->find(1)->as_int() == 2);
    REQUIRE(*k->find(3)->as_int() == -7);
    REQUIRE(*k->find(-1)->as_int() == 1);
    REQUIRE(*k->find(-2)->as_bytes() == "\x0a");
    REQUIRE(*k->find(-3)->as_bytes() == "\x0b");
}

TEST_CASE("cbor: decode_prefix reports what it used", "[cbor]") {
    const auto d = cbor::decode_prefix(hex("8201020a0b"));
    REQUIRE(d.has_value());
    REQUIRE(d->consumed == 3);
    REQUIRE(cbor::decode(hex("8201020a0b")).error() == cbor::Error::trailing_bytes);
}

TEST_CASE("cbor: truncated input at every length is an error, not a crash", "[cbor]") {
    const auto full = hex("a3616101626262436161617465787474657374a2616101616202");
    for (std::size_t n = 0; n < full.size(); ++n) {
        const auto r = cbor::decode_prefix(std::string_view(full).substr(0, n));
        if (r.has_value()) {
            continue; // a shorter prefix can be a complete item
        }
        REQUIRE((r.error() == cbor::Error::truncated || r.error() == cbor::Error::too_large ||
                 r.error() == cbor::Error::bad_text));
    }
    REQUIRE(err_of("") == cbor::Error::truncated);
}

TEST_CASE("cbor: unsupported constructs are refused", "[cbor]") {
    REQUIRE(err_of(hex("5f4101ff")) == cbor::Error::unsupported);           // indefinite byte string
    REQUIRE(err_of(hex("9f01ff")) == cbor::Error::unsupported);             // indefinite array
    REQUIRE(err_of(hex("c001")) == cbor::Error::unsupported);               // tag
    REQUIRE(err_of(hex("f93c00")) == cbor::Error::unsupported);             // half float
    REQUIRE(err_of(hex("fb3ff0000000000000")) == cbor::Error::unsupported); // double
    REQUIRE(err_of(hex("f7")) == cbor::Error::unsupported);                 // undefined
    REQUIRE(err_of(hex("1c")) == cbor::Error::unsupported);                 // reserved additional info
}

TEST_CASE("cbor: duplicate map keys are refused", "[cbor]") {
    REQUIRE(err_of(hex("a2616101616102")) == cbor::Error::duplicate_key);
    REQUIRE(err_of(hex("a201020103")) == cbor::Error::duplicate_key); // {1: 2, 1: 3}
    REQUIRE(cbor::decode(hex("a2616101616202")).has_value());
}

TEST_CASE("cbor: nesting is bounded", "[cbor]") {
    std::string ok;
    for (int i = 0; i < cbor::max_depth; ++i) {
        ok += hex("81");
    }
    ok += hex("00");
    REQUIRE(cbor::decode(ok).has_value());

    std::string deep;
    for (int i = 0; i < cbor::max_depth + 5; ++i) {
        deep += hex("81");
    }
    deep += hex("00");
    REQUIRE(err_of(deep) == cbor::Error::too_deep);

    // Tens of thousands of levels must not overflow the stack.
    std::string bomb(200000, '\x81');
    bomb += '\x00';
    REQUIRE(err_of(bomb) == cbor::Error::too_deep);
}

TEST_CASE("cbor: counts larger than the input cannot allocate", "[cbor]") {
    REQUIRE(err_of(hex("9bffffffffffffffff")) == cbor::Error::too_large); // array of 2^64-1
    REQUIRE(err_of(hex("bbffffffffffffffff")) == cbor::Error::too_large); // map of 2^64-1
    REQUIRE(err_of(hex("9a7fffffff01")) == cbor::Error::too_large);       // 2 billion items, 1 byte left
    REQUIRE(err_of(hex("5bffffffffffffffff")) == cbor::Error::truncated); // byte string of 2^64-1
}

TEST_CASE("cbor: arrays and maps are capped at max_items entries", "[cbor]") {
    // max_items entries decode; one more is refused even though the input holds every byte.
    const auto array_of = [](std::uint64_t n) -> std::string {
        std::string s = hex("99") + static_cast<char>(n >> 8U) + static_cast<char>(n & 0xFFU);
        s.append(n, '\x00');
        return s;
    };
    const auto map_of = [](std::uint64_t n) -> std::string { // distinct integer keys 0..n-1, null values
        std::string s = hex("b9") + static_cast<char>(n >> 8U) + static_cast<char>(n & 0xFFU);
        for (std::uint64_t i = 0; i < n; ++i) {
            s += hex("19") + static_cast<char>(i >> 8U) + static_cast<char>(i & 0xFFU);
            s += '\xf6';
        }
        return s;
    };
    REQUIRE(cbor::decode(array_of(cbor::max_items)).has_value());
    REQUIRE(err_of(array_of(cbor::max_items + 1)) == cbor::Error::too_large);
    REQUIRE(cbor::decode(map_of(cbor::max_items)).has_value());
    REQUIRE(err_of(map_of(cbor::max_items + 1)) == cbor::Error::too_large);
    // A large map that would make the duplicate-key check quadratic is refused before any entry is read.
    REQUIRE(err_of(map_of(20000)) == cbor::Error::too_large);
}
