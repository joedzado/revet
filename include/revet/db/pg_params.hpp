#pragma once
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

namespace db::postgres::detail {

// Convert a typed C++ value to a PostgreSQL text-protocol parameter string.
// PQexecParams always uses text format (last arg = 0), so all params must be
// valid Postgres literal representations of their type.
template<typename T>
inline std::string to_pg_param(T&& arg) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, bool>) {
        return arg ? "true" : "false";
    } else if constexpr (std::is_floating_point_v<U>) {
        return std::format("{:.17g}", static_cast<double>(arg));
    } else if constexpr (std::is_integral_v<U>) {
        return std::format("{}", static_cast<int64_t>(arg));
    } else {
        return std::string(std::forward<T>(arg));
    }
}

} // namespace db::postgres::detail
