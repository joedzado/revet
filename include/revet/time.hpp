#pragma once
// Dates and times as apps show them: local calendar days, ISO dates, "3 h ago".
// Times are Unix epoch milliseconds (int64) unless a type says otherwise.

#include <chrono>
#include <cstdint>
#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace revet::time {

inline auto now_ms() -> int64_t {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Local broken-down time (honours TZ).
inline auto local_tm(int64_t ms) -> std::tm {
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm out{};
    localtime_r(&t, &out);
    return out;
}

inline auto utc_tm(int64_t ms) -> std::tm {
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm out{};
    gmtime_r(&t, &out);
    return out;
}

// strftime over local time: format_local(ms, "%H:%M") -> "07:05".
inline auto format_local(int64_t ms, const char* fmt) -> std::string {
    const std::tm tm = local_tm(ms);
    char buf[128];
    const size_t n = std::strftime(buf, sizeof(buf), fmt, &tm);
    return {buf, n};
}

// Seconds east of UTC in effect at that instant (DST-aware).
inline auto utc_offset_s(int64_t ms) -> long {
    return local_tm(ms).tm_gmtoff;
}

// "2026-11-08"
inline auto iso_date(std::chrono::year_month_day d) -> std::string {
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(d.year()), static_cast<unsigned>(d.month()),
                       static_cast<unsigned>(d.day()));
}

// The local calendar day containing `ms`.
inline auto local_day(int64_t ms) -> std::chrono::year_month_day {
    const std::tm tm = local_tm(ms);
    return std::chrono::year{tm.tm_year + 1900} / std::chrono::month{static_cast<unsigned>(tm.tm_mon + 1)} /
           std::chrono::day{static_cast<unsigned>(tm.tm_mday)};
}

inline auto today() -> std::chrono::year_month_day {
    return local_day(now_ms());
}

// Today as "YYYY-MM-DD", local time.
inline auto today_iso() -> std::string {
    return iso_date(today());
}

// Parses "YYYY-MM-DD" (anything after the date, like "T12:00:00Z", is ignored). Rejects 2026-02-30.
inline auto parse_date(std::string_view s) -> std::optional<std::chrono::year_month_day> {
    if (s.size() < 10 || s[4] != '-' || s[7] != '-') {
        return std::nullopt;
    }
    auto digits = [&](size_t pos, size_t n) -> std::optional<int> {
        int v = 0;
        for (size_t i = pos; i < pos + n; ++i) {
            if (s[i] < '0' || s[i] > '9') {
                return std::nullopt;
            }
            v = v * 10 + (s[i] - '0');
        }
        return v;
    };
    const auto y = digits(0, 4);
    const auto m = digits(5, 2);
    const auto d = digits(8, 2);
    if (!y || !m || !d) {
        return std::nullopt;
    }
    const std::chrono::year_month_day ymd{std::chrono::year{*y}, std::chrono::month{static_cast<unsigned>(*m)},
                                          std::chrono::day{static_cast<unsigned>(*d)}};
    if (!ymd.ok()) {
        return std::nullopt;
    }
    return ymd;
}

// Whole days from a to b (negative if b is earlier).
inline auto days_between(std::chrono::year_month_day a, std::chrono::year_month_day b) -> int {
    return static_cast<int>((std::chrono::sys_days{b} - std::chrono::sys_days{a}).count());
}

// How long ago, coarsely: "just now", "12 min ago", "5 h ago", "3 days ago", "4 months ago", "2 years ago".
// A time in the future reads "just now".
inline auto ago(int64_t ms, int64_t now = now_ms()) -> std::string {
    const int64_t s = (now - ms) / 1000;
    if (s < 90) {
        return "just now";
    }
    if (s < 90 * 60) {
        return std::format("{} min ago", s / 60);
    }
    if (s < 48 * 3600) {
        return std::format("{} h ago", s / 3600);
    }
    const int64_t days = s / 86400;
    if (days < 60) {
        return std::format("{} days ago", days);
    }
    if (days < 730) {
        return std::format("{} months ago", days / 30);
    }
    return std::format("{} years ago", days / 365);
}

// ago() for a calendar date: "today", "yesterday", "3 days ago", "4 months ago", ...
inline auto ago(std::chrono::year_month_day day, std::chrono::year_month_day ref = today()) -> std::string {
    const int days = days_between(day, ref);
    if (days <= 0) {
        return "today";
    }
    if (days == 1) {
        return "yesterday";
    }
    if (days < 60) {
        return std::format("{} days ago", days);
    }
    if (days < 730) {
        return std::format("{} months ago", days / 30);
    }
    return std::format("{} years ago", days / 365);
}

} // namespace revet::time
