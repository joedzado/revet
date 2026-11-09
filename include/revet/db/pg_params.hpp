#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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
    } else if constexpr (requires { arg.to_string(); } && !std::is_convertible_v<U, std::string_view>) {
        return arg.to_string(); // revet::Decimal and similar
    } else {
        return std::string(std::forward<T>(arg));
    }
}

template<typename T>
struct is_optional : std::false_type {};
template<typename T>
struct is_optional<std::optional<T>> : std::true_type {};

// As to_pg_param, but nullptr and an empty std::optional bind as SQL NULL.
template<typename T>
inline std::optional<std::string> to_nullable_pg_param(T&& arg) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, std::nullptr_t>) {
        return std::nullopt;
    } else if constexpr (is_optional<U>::value) {
        if (!arg) {
            return std::nullopt;
        }
        return to_pg_param(*std::forward<T>(arg));
    } else {
        return to_pg_param(std::forward<T>(arg));
    }
}

// Translate ? placeholders to $1, $2, ... for PQexecParams. A ? inside a string
// literal, quoted identifier, comment or dollar-quoted body is left alone. Write
// ?? for a literal ? operator (for example the jsonb "has key" operator).
inline std::string positional_sql(std::string_view sql) {
    std::string out;
    out.reserve(sql.size() + 16);
    int next_param = 1;
    size_t i = 0;

    const auto copy_until = [&](size_t end) {
        out.append(sql.substr(i, end - i));
        i = end;
    };
    const auto is_ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };

    while (i < sql.size()) {
        const char c = sql[i];
        const char next = i + 1 < sql.size() ? sql[i + 1] : '\0';

        if (c == '\'' || c == '"') {
            // E'...' strings also escape with a backslash. The prefix only counts
            // when it is a whole token, so name'x' style text is not misread.
            const bool backslash = c == '\'' && i > 0 && (sql[i - 1] == 'e' || sql[i - 1] == 'E') &&
                                   (i < 2 || !is_ident(sql[i - 2]));
            size_t j = i + 1;
            while (j < sql.size()) {
                if (backslash && sql[j] == '\\') {
                    j += 2;
                } else if (sql[j] == c) {
                    // A doubled quote is an escaped quote, not the end.
                    if (j + 1 < sql.size() && sql[j + 1] == c) {
                        j += 2;
                    } else {
                        break;
                    }
                } else {
                    ++j;
                }
            }
            copy_until(std::min(j + 1, sql.size()));
        } else if (c == '-' && next == '-') {
            const size_t nl = sql.find('\n', i);
            copy_until(nl == std::string_view::npos ? sql.size() : nl);
        } else if (c == '/' && next == '*') {
            // Postgres block comments nest.
            size_t j = i + 2;
            int depth = 1;
            while (j < sql.size() && depth > 0) {
                if (sql.substr(j, 2) == "/*") {
                    ++depth;
                    j += 2;
                } else if (sql.substr(j, 2) == "*/") {
                    --depth;
                    j += 2;
                } else {
                    ++j;
                }
            }
            copy_until(std::min(j, sql.size()));
        } else if (c == '$' && (i == 0 || !is_ident(sql[i - 1]))) {
            // $tag$ ... $tag$ (tag may be empty). Anything else, like $1, is copied.
            size_t tag_end = i + 1;
            while (tag_end < sql.size() && is_ident(sql[tag_end])) {
                ++tag_end;
            }
            const bool tag_ok = tag_end < sql.size() && sql[tag_end] == '$' &&
                                std::isdigit(static_cast<unsigned char>(sql[i + 1])) == 0;
            if (tag_ok) {
                const std::string_view tag = sql.substr(i, tag_end - i + 1);
                const size_t close = sql.find(tag, tag_end + 1);
                copy_until(close == std::string_view::npos ? sql.size() : close + tag.size());
            } else {
                out += c;
                ++i;
            }
        } else if (c == '?' && next == '?') {
            out += '?';
            i += 2;
        } else if (c == '?') {
            out += std::format("${}", next_param++);
            ++i;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

} // namespace db::postgres::detail
