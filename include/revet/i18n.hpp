#pragma once
#include <string_view>

// ── i18n:: compile-time string table ──────────────────────────────────────────
//
// Locale is set once per request via i18n::set_locale(name) in middleware.
// Str<N> is a constexpr array of N translations indexed by locale.
// operator string_view() makes it drop-in wherever a string is expected:
//   html::h1{i18n::page_home_h1}   // no locale argument needed

namespace i18n {

namespace detail {
inline thread_local int locale_idx = 0;
}

template<int N>
struct Str {
    std::string_view data[N];
    auto get() const -> std::string_view {
        return data[detail::locale_idx];
    }
    operator std::string_view() const {
        return get();
    }
};

// set_locale(name) and the Locale enum are generated per-project in i18n.hpp.

} // namespace i18n
