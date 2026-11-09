#ifndef REVET_DB_COMMON_HPP
#define REVET_DB_COMMON_HPP

#pragma once
#include <charconv>
#include <format>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace db {
struct Read {};  // read-only handle — no write lock acquired
struct Write {}; // write handle — acquires pool write lock on construction

using Row = std::vector<std::string>;
using Result = std::vector<Row>;

namespace detail {
template<typename T>
struct is_optional : std::false_type {};
template<typename T>
struct is_optional<std::optional<T>> : std::true_type {};

// Column text -> T. std::optional<T> takes NULL; any other T throws on NULL or bad text.
template<typename T>
auto convert(const std::optional<std::string>& cell, size_t col) -> T {
    if constexpr (is_optional<T>::value) {
        if (!cell) {
            return std::nullopt;
        }
        return convert<typename T::value_type>(cell, col);
    } else {
        if (!cell) {
            throw std::runtime_error(std::format("db: column {} is NULL; read it as std::optional", col));
        }
        const std::string& s = *cell;
        if constexpr (std::is_same_v<T, std::string>) {
            return s;
        } else if constexpr (std::is_same_v<T, bool>) {
            // SQLite stores 0/1, Postgres prints t/f.
            return !(s.empty() || s == "0" || s == "f" || s == "false");
        } else if constexpr (std::is_arithmetic_v<T>) {
            T out{};
            const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
            if (ec != std::errc{} || ptr != s.data() + s.size()) {
                // SQLite may print an integer-valued REAL as "3.0".
                if constexpr (std::is_integral_v<T>) {
                    double d = 0;
                    const auto [p2, e2] = std::from_chars(s.data(), s.data() + s.size(), d);
                    if (e2 == std::errc{} && p2 == s.data() + s.size() && d == static_cast<double>(static_cast<T>(d))) {
                        return static_cast<T>(d);
                    }
                }
                throw std::runtime_error(std::format("db: column {} holds \"{}\", not a number", col, s));
            }
            return out;
        } else {
            // Any type constructible from text, such as revet::Decimal via its parse().
            if constexpr (requires { T::parse(s); }) {
                return T::parse(s);
            } else {
                return T(s);
            }
        }
    }
}

template<typename... Cols, typename RowT, size_t... I>
auto typed_row(const RowT& row, std::index_sequence<I...>) -> std::tuple<Cols...> {
    if (row.size() < sizeof...(Cols)) {
        throw std::runtime_error(std::format("db: query returned {} columns, expected {}", row.size(), sizeof...(Cols)));
    }
    return std::tuple<Cols...>{convert<Cols>(std::optional<std::string>(row[I]), I)...};
}
} // namespace detail

// Typed rows from a NullableResult: auto rows = db::typed<std::string, double, std::optional<int>>(raw);
// for (auto [name, total, count] : rows) ...
template<typename... Cols, typename ResultT>
auto typed(const ResultT& raw) -> std::vector<std::tuple<Cols...>> {
    std::vector<std::tuple<Cols...>> out;
    out.reserve(raw.size());
    for (const auto& row : raw) {
        out.push_back(detail::typed_row<Cols...>(row, std::index_sequence_for<Cols...>{}));
    }
    return out;
}
} // namespace db

#endif
