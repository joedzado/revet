#pragma once

#include <array>
#include <expected>
#include <format>
#include <revet/ctre.hpp>
#include <revet/web.hpp>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace validate {

template<std::size_t N>
struct msg_str {
    std::array<char, N> data{};
    // A string literal binds only to a C array reference.
    constexpr msg_str(const char (&s)[N]) { // NOLINT(*-avoid-c-arrays)
        std::copy_n(s, N, data.begin());
    }
    constexpr operator std::string_view() const {
        return {data.data(), N - 1};
    }
};

// ── Result ────────────────────────────────────────────────────────────────────

struct Error {
    std::string field;
    std::string message;
};

using Result = std::expected<void, std::vector<Error>>;

// ── Rules ─────────────────────────────────────────────────────────────────────

struct required_t {
    static constexpr auto check(std::string_view val) -> bool {
        return !val.empty();
    }
    static auto msg() -> std::string { // NOLINT(modernize-use-string-view): same type as the other rules
        return "required";
    }
};
inline constexpr required_t required;

template<std::size_t N>
struct min_len_t {
    static constexpr auto check(std::string_view val) -> bool {
        return val.size() >= N;
    }
    static auto msg() -> std::string {
        return std::format("min length {}", N);
    }
};
template<std::size_t N>
inline constexpr min_len_t<N> min_len;

template<std::size_t N>
struct max_len_t {
    static constexpr auto check(std::string_view val) -> bool {
        return val.size() <= N;
    }
    static auto msg() -> std::string {
        return std::format("max length {}", N);
    }
};
template<std::size_t N>
inline constexpr max_len_t<N> max_len;

template<ctll::fixed_string Pattern, msg_str Msg = "invalid format">
struct regex_t {
    static auto check(std::string_view val) -> bool {
        return static_cast<bool>(ctre::match<Pattern>(val));
    }
    static auto msg() -> std::string {
        return std::string(std::string_view(Msg));
    }
};
template<ctll::fixed_string Pattern>
inline constexpr regex_t<Pattern> regex;

// ── Field ─────────────────────────────────────────────────────────────────────

template<typename... Rules>
struct Field {
    std::string_view name;
    std::tuple<Rules...> rules;

    constexpr Field(std::string_view n, Rules... rs) : name(n), rules(rs...) {}

    void validate(std::string_view val, std::vector<Error>& out) const {
        std::apply([&val, &out, this](const auto&... rule) -> auto { (check_rule(rule, val, out), ...); }, rules);
    }

private:
    void check_rule(const auto& rule, std::string_view val, std::vector<Error>& out) const {
        if (!rule.check(val)) {
            out.push_back({std::string(name), rule.msg()});
        }
    }
};

// ── Schema ────────────────────────────────────────────────────────────────────

template<typename... Fields>
struct Schema {
    std::tuple<Fields...> fields;
    constexpr explicit Schema(Fields... fs) : fields(fs...) {}
};

// ── Factories ─────────────────────────────────────────────────────────────────

template<typename... Rules>
constexpr auto field(std::string_view name, Rules... rules) {
    return Field<Rules...>(name, rules...);
}

template<typename... Fields>
constexpr auto schema(Fields... fs) {
    return Schema<Fields...>(fs...);
}

// ── check() ───────────────────────────────────────────────────────────────────

template<typename... Fields>
auto check(const web::ParamMap& data, const Schema<Fields...>& s) -> Result {
    std::vector<Error> errs;
    std::apply([&](const auto&... f) -> auto { (f.validate(data.get(f.name), errs), ...); }, s.fields);
    if (errs.empty()) {
        return {};
    }
    return std::unexpected(std::move(errs));
}

} // namespace validate
