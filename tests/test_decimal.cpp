#include <catch2/catch_test_macros.hpp>
#include <revet/decimal.hpp>
#include <string>

using revet::Decimal;
using revet::DecimalError;
using revet::Rounding;

namespace {
Decimal d(const char* s) {
    return Decimal::parse(s);
}
} // namespace

TEST_CASE("decimal: parse and print keep the exact text") {
    for (const char* s : {"0", "0.00", "1", "-1", "12.3400", "0.1000", "-0.0001", "123456789012345.6789",
                          "0.000000000000000001", "99999999999999999999999999999999999999"}) {
        CHECK(d(s).to_string() == s);
    }
    CHECK(d("+1.5").to_string() == "1.5");
    CHECK(d(".5").to_string() == "0.5");
    CHECK(d("-0.00").to_string() == "0.00");
    CHECK(d("007").to_string() == "7");
}

TEST_CASE("decimal: invalid text throws") {
    for (const char* s : {"", "-", ".", "1.", "abc", "1.2.3", "1e5", "NaN", "Infinity", " 1", "1 ", "1,000", "--1"}) {
        INFO(s);
        CHECK_THROWS_AS(d(s), DecimalError);
    }
}

TEST_CASE("decimal: 0.1 + 0.2 is exactly 0.3") {
    CHECK((d("0.1") + d("0.2")).to_string() == "0.3");
    CHECK(d("0.1") + d("0.2") == d("0.3"));
}

TEST_CASE("decimal: scale rules match Postgres") {
    CHECK((d("1.20") + d("3")).to_string() == "4.20");
    CHECK((d("1.20") - d("3.000")).to_string() == "-1.800");
    CHECK((d("1.20") * d("3.0")).to_string() == "3.600");
    CHECK((d("-0.5") * d("0.5")).to_string() == "-0.25");
    CHECK((-d("1.50")).to_string() == "-1.50");
}

TEST_CASE("decimal: comparison is by value") {
    CHECK(d("1.50") == d("1.5"));
    CHECK(d("1.49") < d("1.5"));
    CHECK(d("-2") < d("-1.99"));
    CHECK(d("0.00") == d("0"));
}

TEST_CASE("decimal: compound assignment accumulates exactly") {
    Decimal total;
    for (int i = 0; i < 1000; ++i) {
        total += d("0.0001");
    }
    CHECK(total.to_string() == "0.1000");
    total -= d("0.1");
    CHECK(total.is_zero());
}

TEST_CASE("decimal: rounding") {
    CHECK(d("1.005").rescale(2).to_string() == "1.01");
    CHECK(d("-1.005").rescale(2).to_string() == "-1.01");
    CHECK(d("1.004").rescale(2).to_string() == "1.00");
    CHECK(d("1.5").rescale(3).to_string() == "1.500");

    CHECK(d("1.005").rescale(2, Rounding::HalfEven).to_string() == "1.00");
    CHECK(d("1.015").rescale(2, Rounding::HalfEven).to_string() == "1.02");
    CHECK(d("-1.005").rescale(2, Rounding::HalfEven).to_string() == "-1.00");
    CHECK(d("-1.015").rescale(2, Rounding::HalfEven).to_string() == "-1.02");
    CHECK(d("-0.004").rescale(2).to_string() == "0.00");
}

TEST_CASE("decimal: division") {
    CHECK(d("1").divide(d("3"), 4).to_string() == "0.3333");
    CHECK(d("2").divide(d("3"), 4).to_string() == "0.6667");
    CHECK(d("-2").divide(d("3"), 4).to_string() == "-0.6667");
    CHECK(d("2").divide(d("-3"), 4).to_string() == "-0.6667");
    CHECK(d("1.00").divide(d("8"), 2).to_string() == "0.13");
    CHECK(d("1.00").divide(d("8"), 2, Rounding::HalfEven).to_string() == "0.12");
    CHECK(d("10.5000").divide(d("0.25"), 0).to_string() == "42");
    CHECK_THROWS_AS(d("1").divide(d("0.00"), 2), DecimalError);
}

TEST_CASE("decimal: overflow throws instead of wrapping") {
    const auto big = d("99999999999999999999999999999999999999");
    CHECK_THROWS_AS(big * big, DecimalError);
    CHECK_THROWS_AS(d("170141183460469231731687303715884105728"), DecimalError);
    CHECK_THROWS_AS(d("170141183460469231731687303715884105727") + d("1"), DecimalError);
    CHECK_THROWS_AS(-d("170141183460469231731687303715884105727") - d("1"), DecimalError);
    CHECK_THROWS_AS(d("1").rescale(39), DecimalError);
    CHECK_THROWS_AS(d("1").rescale(-1), DecimalError);
    CHECK_THROWS_AS(big.rescale(38), DecimalError);
    CHECK_THROWS_AS(d("0.0000000000000000000000000000000000001") * d("0.01"), DecimalError);
}

TEST_CASE("decimal: Sure's largest column shapes fit") {
    // numeric(19,4) sums.
    const auto balance = d("999999999999999.9999");
    Decimal sum;
    for (int i = 0; i < 100000; ++i) {
        sum += balance;
    }
    CHECK(sum.to_string() == "99999999999999999990.0000");

    // numeric(34,18) qty times numeric(19,10) price at realistic sizes.
    CHECK((d("1234.567890123456789012") * d("512.3456789012")).to_string() ==
          "632525.5238149245548493153483936144");
}
