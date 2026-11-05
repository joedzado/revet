#pragma once

#include <compare>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

// Exact decimal arithmetic for money, matching Postgres numeric: a 128-bit integer
// and a scale, so 12.3400 is 123400 at scale 4. Holds up to 38 significant digits.
// Any result that does not fit throws DecimalError instead of losing precision.
//
// Scale follows Postgres: + and - use the larger scale, * adds the scales.
// to_string() keeps trailing zeros, so a value reads back as Postgres printed it.
namespace revet {

class DecimalError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Rounding : std::uint8_t {
    HalfUp,  // ties away from zero: Postgres round(), Ruby BigDecimal's default
    HalfEven // ties to the even neighbor
};

class Decimal {
public:
    Decimal() = default;

    // Accepts [+-]digits[.digits], the form Postgres prints. NaN, Infinity and
    // exponents are rejected.
    static Decimal parse(std::string_view text) {
        std::string_view s = text;
        bool negative = false;
        if (!s.empty() && (s.front() == '-' || s.front() == '+')) {
            negative = s.front() == '-';
            s.remove_prefix(1);
        }
        const auto dot = s.find('.');
        const auto whole = s.substr(0, dot);
        const auto frac = dot == std::string_view::npos ? std::string_view{} : s.substr(dot + 1);
        if (whole.empty() && frac.empty()) {
            throw DecimalError("decimal: invalid number");
        }
        if (dot != std::string_view::npos && frac.empty()) {
            throw DecimalError("decimal: invalid number");
        }
        wide value = 0;
        for (const auto part : {whole, frac}) {
            for (const char c : part) {
                if (c < '0' || c > '9') {
                    throw DecimalError("decimal: invalid number");
                }
                value = add_checked(mul_checked(value, 10), c - '0');
            }
        }
        if (frac.size() > kMaxScale) {
            throw DecimalError("decimal: scale too large");
        }
        return {negative ? -value : value, static_cast<int>(frac.size())};
    }

    static Decimal from_int(std::int64_t value) {
        return {value, 0};
    }

    [[nodiscard]] int scale() const {
        return scale_;
    }
    [[nodiscard]] bool is_zero() const {
        return value_ == 0;
    }
    [[nodiscard]] bool is_negative() const {
        return value_ < 0;
    }

    [[nodiscard]] std::string to_string() const {
        std::string digits;
        wide v = value_ < 0 ? -value_ : value_;
        do {
            digits.insert(digits.begin(), static_cast<char>('0' + static_cast<int>(v % 10)));
            v /= 10;
        } while (v != 0);
        if (scale_ > 0) {
            const auto s = static_cast<std::size_t>(scale_);
            if (digits.size() <= s) {
                digits.insert(0, s - digits.size() + 1, '0');
            }
            digits.insert(digits.size() - s, 1, '.');
        }
        return value_ < 0 ? "-" + digits : digits;
    }

    friend Decimal operator+(const Decimal& a, const Decimal& b) {
        const auto [x, y, scale] = aligned(a, b);
        return {add_checked(x, y), scale};
    }
    friend Decimal operator-(const Decimal& a, const Decimal& b) {
        const auto [x, y, scale] = aligned(a, b);
        return {add_checked(x, -y), scale};
    }
    friend Decimal operator*(const Decimal& a, const Decimal& b) {
        const int scale = a.scale_ + b.scale_;
        if (scale > kMaxScale) {
            throw DecimalError("decimal: scale too large");
        }
        return {mul_checked(a.value_, b.value_), scale};
    }
    Decimal operator-() const {
        return {-value_, scale_};
    }
    Decimal& operator+=(const Decimal& o) {
        return *this = *this + o;
    }
    Decimal& operator-=(const Decimal& o) {
        return *this = *this - o;
    }

    // Value comparison: 1.50 equals 1.5.
    friend bool operator==(const Decimal& a, const Decimal& b) {
        return (a <=> b) == std::strong_ordering::equal;
    }
    friend std::strong_ordering operator<=>(const Decimal& a, const Decimal& b) {
        const auto [x, y, scale] = aligned(a, b);
        (void)scale;
        return x <=> y;
    }

    // Changes scale, rounding if digits are dropped.
    [[nodiscard]] Decimal rescale(int new_scale, Rounding mode = Rounding::HalfUp) const {
        check_scale(new_scale);
        if (new_scale >= scale_) {
            return {mul_checked(value_, pow10(new_scale - scale_)), new_scale};
        }
        return {divide_rounded(value_, pow10(scale_ - new_scale), mode), new_scale};
    }

    // a / b at the requested scale. Throws on division by zero.
    [[nodiscard]] Decimal divide(const Decimal& divisor, int result_scale, Rounding mode = Rounding::HalfUp) const {
        check_scale(result_scale);
        if (divisor.value_ == 0) {
            throw DecimalError("decimal: division by zero");
        }
        // value / 10^s ÷ d / 10^t = (value * 10^(r + t - s)) / d, at scale r.
        const int shift = result_scale + divisor.scale_ - scale_;
        if (shift >= 0) {
            return {divide_rounded(mul_checked(value_, pow10(shift)), divisor.value_, mode), result_scale};
        }
        return {divide_rounded(value_, mul_checked(divisor.value_, pow10(-shift)), mode), result_scale};
    }

private:
    __extension__ typedef __int128 wide;
    __extension__ typedef unsigned __int128 uwide;

    static constexpr int kMaxScale = 38;
    static constexpr wide kMax = static_cast<wide>((static_cast<uwide>(1) << 127) - 1);

    Decimal(wide value, int scale) : value_(value), scale_(scale) {}

    static void check_scale(int scale) {
        if (scale < 0 || scale > kMaxScale) {
            throw DecimalError("decimal: scale out of range");
        }
    }

    static wide pow10(int n) {
        wide r = 1;
        for (int i = 0; i < n; ++i) {
            r = mul_checked(r, 10);
        }
        return r;
    }

    static wide add_checked(wide a, wide b) {
        if ((b > 0 && a > kMax - b) || (b < 0 && a < -kMax - b)) {
            throw DecimalError("decimal: overflow");
        }
        return a + b;
    }

    static wide mul_checked(wide a, wide b) {
        if (a == 0 || b == 0) {
            return 0;
        }
        const wide abs_a = a < 0 ? -a : a;
        const wide abs_b = b < 0 ? -b : b;
        if (abs_a > kMax / abs_b) {
            throw DecimalError("decimal: overflow");
        }
        return a * b;
    }

    static wide divide_rounded(wide n, wide d, Rounding mode) {
        wide q = n / d;
        const wide r = n % d;
        if (r == 0) {
            return q;
        }
        const wide abs_r = r < 0 ? -r : r;
        const wide abs_d = d < 0 ? -d : d;
        // Compare remainder to half the divisor without overflowing r * 2.
        const wide other = abs_d - abs_r;
        const bool above_half = abs_r > other;
        const bool tie = abs_r == other;
        if (above_half || (tie && (mode == Rounding::HalfUp || q % 2 != 0))) {
            q += ((n < 0) != (d < 0)) ? -1 : 1;
        }
        return q;
    }

    struct Aligned {
        wide x;
        wide y;
        int scale;
    };
    static Aligned aligned(const Decimal& a, const Decimal& b) {
        if (a.scale_ == b.scale_) {
            return {a.value_, b.value_, a.scale_};
        }
        if (a.scale_ > b.scale_) {
            return {a.value_, mul_checked(b.value_, pow10(a.scale_ - b.scale_)), a.scale_};
        }
        return {mul_checked(a.value_, pow10(b.scale_ - a.scale_)), b.value_, b.scale_};
    }

    wide value_ = 0;
    int scale_ = 0;
};

} // namespace revet
