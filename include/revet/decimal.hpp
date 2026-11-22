#pragma once

#include <algorithm>
#include <compare>
#include <cstdint>
#include <ranges>
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
    static auto parse(std::string_view text) -> Decimal {
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
        auto is_digit = [](char c) -> bool { return c >= '0' && c <= '9'; };
        if (!std::ranges::all_of(whole, is_digit) || !std::ranges::all_of(frac, is_digit)) {
            throw DecimalError("decimal: invalid number");
        }
        auto shift_in = [](wide acc, char c) -> wide { return add_checked(mul_checked(acc, 10), c - '0'); };
        const wide value = std::ranges::fold_left(frac, std::ranges::fold_left(whole, wide{0}, shift_in), shift_in);
        if (frac.size() > kMaxScale) {
            throw DecimalError("decimal: scale too large");
        }
        return {negative ? -value : value, static_cast<int>(frac.size())};
    }

    static auto from_int(std::int64_t value) -> Decimal {
        return {value, 0};
    }

    [[nodiscard]] auto scale() const -> int {
        return scale_;
    }
    [[nodiscard]] auto is_zero() const -> bool {
        return value_ == 0;
    }
    [[nodiscard]] auto is_negative() const -> bool {
        return value_ < 0;
    }

    [[nodiscard]] auto to_string() const -> std::string {
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

    friend auto operator+(const Decimal& a, const Decimal& b) -> Decimal {
        const auto [x, y, scale] = aligned(a, b);
        return {add_checked(x, y), scale};
    }
    friend auto operator-(const Decimal& a, const Decimal& b) -> Decimal {
        const auto [x, y, scale] = aligned(a, b);
        return {add_checked(x, -y), scale};
    }
    friend auto operator*(const Decimal& a, const Decimal& b) -> Decimal {
        const int scale = a.scale_ + b.scale_;
        if (scale > kMaxScale) {
            throw DecimalError("decimal: scale too large");
        }
        return {mul_checked(a.value_, b.value_), scale};
    }
    auto operator-() const -> Decimal {
        return {-value_, scale_};
    }
    auto operator+=(const Decimal& o) -> Decimal& {
        return *this = *this + o;
    }
    auto operator-=(const Decimal& o) -> Decimal& {
        return *this = *this - o;
    }

    // Value comparison: 1.50 equals 1.5.
    friend auto operator==(const Decimal& a, const Decimal& b) -> bool {
        return (a <=> b) == std::strong_ordering::equal;
    }
    friend auto operator<=>(const Decimal& a, const Decimal& b) -> std::strong_ordering {
        const auto [x, y, scale] = aligned(a, b);
        (void)scale;
        return x <=> y;
    }

    // Changes scale, rounding if digits are dropped.
    [[nodiscard]] auto rescale(int new_scale, Rounding mode = Rounding::HalfUp) const -> Decimal {
        check_scale(new_scale);
        if (new_scale >= scale_) {
            return {mul_checked(value_, pow10(new_scale - scale_)), new_scale};
        }
        return {divide_rounded(value_, pow10(scale_ - new_scale), mode), new_scale};
    }

    // a / b at the requested scale. Throws on division by zero.
    [[nodiscard]] auto divide(const Decimal& divisor, int result_scale,
                              Rounding mode = Rounding::HalfUp) const -> Decimal {
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
    __extension__ using wide = __int128;
    __extension__ using uwide = unsigned __int128;

    static constexpr int kMaxScale = 38;
    static constexpr wide kMax = static_cast<wide>((static_cast<uwide>(1) << 127) - 1);

    Decimal(wide value, int scale) : value_(value), scale_(scale) {}

    static void check_scale(int scale) {
        if (scale < 0 || scale > kMaxScale) {
            throw DecimalError("decimal: scale out of range");
        }
    }

    static auto pow10(int n) -> wide {
        return std::ranges::fold_left(std::views::repeat(wide{10}, n), wide{1}, mul_checked);
    }

    static auto add_checked(wide a, wide b) -> wide {
        if ((b > 0 && a > kMax - b) || (b < 0 && a < -kMax - b)) {
            throw DecimalError("decimal: overflow");
        }
        return a + b;
    }

    static auto mul_checked(wide a, wide b) -> wide {
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

    static auto divide_rounded(wide n, wide d, Rounding mode) -> wide {
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
    static auto aligned(const Decimal& a, const Decimal& b) -> Aligned {
        if (a.scale_ == b.scale_) {
            return {.x = a.value_, .y = b.value_, .scale = a.scale_};
        }
        if (a.scale_ > b.scale_) {
            return {.x = a.value_, .y = mul_checked(b.value_, pow10(a.scale_ - b.scale_)), .scale = a.scale_};
        }
        return {.x = mul_checked(a.value_, pow10(b.scale_ - a.scale_)), .y = b.value_, .scale = b.scale_};
    }

    wide value_ = 0;
    int scale_ = 0;
};

} // namespace revet
