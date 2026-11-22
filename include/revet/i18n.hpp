#pragma once
#include <array>
#include <string_view>

// ── i18n:: compile-time string table ──────────────────────────────────────────
//
// Locale is set once per request via i18n::set_locale(name) in middleware.
// Str<N> is a constexpr array of N translations indexed by locale.
// operator string_view() makes it drop-in wherever a string is expected:
//   html::h1{i18n::page_home_h1}   // no locale argument needed

namespace i18n {

namespace detail {
// The current request's locale, as an index into the generated locale list.
inline auto locale_idx() -> int& {
    thread_local int idx = 0;
    return idx;
}
} // namespace detail

template<int N>
struct Str {
    std::array<std::string_view, N> data;
    [[nodiscard]] auto get() const -> std::string_view {
        return data.at(detail::locale_idx());
    }
    // NOLINTBEGIN(*-explicit-constructor): implicit on purpose, translated strings are passed directly as text
    operator std::string_view() const {
        // NOLINTEND(*-explicit-constructor)
        return get();
    }
};

// set_locale(name) and the Locale enum are generated per-project in i18n.hpp.

} // namespace i18n
