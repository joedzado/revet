#pragma once

#include <chrono>
#include <cstdlib>
#include <format>
#include <mutex>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>

namespace log {

// ── Tag Registry ──────────────────────────────────────────────────────────────

namespace tags {
constexpr std::string_view FRAMEWORK = "framework";
constexpr std::string_view ROUTES = "routes";
constexpr std::string_view DB = "db";
constexpr std::string_view APP = "app";
} // namespace tags

// ── Level Enum ────────────────────────────────────────────────────────────────

enum class Level : unsigned char {
    DEBUG,
    INFO,
    WARN,
    ERROR,
};

// ── Internal Implementation ───────────────────────────────────────────────────

namespace detail {
inline std::mutex log_mutex;
inline thread_local std::string_view current_tag = tags::FRAMEWORK;
inline Level current_level = Level::INFO;

inline auto level_name(Level lvl) -> std::string_view {
    switch (lvl) {
        case Level::DEBUG:
            return "DEBUG";
        case Level::INFO:
            return "INFO";
        case Level::WARN:
            return "WARN";
        case Level::ERROR:
            return "ERROR";
    }
    return "UNKNOWN";
}

inline auto timestamp() -> std::string {
    auto now = std::chrono::system_clock::now();
    return std::format("{:%Y-%m-%d %H:%M:%S}", now);
}

inline void write_impl(Level lvl, std::string_view tag, std::string_view msg) {
    if (lvl < current_level) {
        return;
    }

    std::string output = std::format("[{}] {} [{}] {}", timestamp(), level_name(lvl), tag, msg);
    std::lock_guard<std::mutex> lock(log_mutex);
    std::println(stderr, "{}", output);
}
} // namespace detail

// ── Public Control API ─────────────────────────────────────────────────────────

inline void set_level(Level lvl) {
    detail::current_level = lvl;
}

inline auto get_level() -> Level {
    return detail::current_level;
}

inline void init_from_env() {
    const char* env_level = std::getenv("LOG_LEVEL");
    if (!env_level) {
        return;
    }

    static const std::unordered_map<std::string_view, Level> table = {
        {"debug", Level::DEBUG},
        {"info", Level::INFO},
        {"warn", Level::WARN},
        {"error", Level::ERROR},
    };
    if (auto it = table.find(std::string_view(env_level)); it != table.end()) {
        set_level(it->second);
    }
}

// ── Logging API: Default (uses TU tag) ──────────────────────────────────────────

template<typename... Args>
inline void debug(std::format_string<Args...> fmt, Args&&... args) {
    detail::write_impl(Level::DEBUG, detail::current_tag, std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void info(std::format_string<Args...> fmt, Args&&... args) {
    detail::write_impl(Level::INFO, detail::current_tag, std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void warn(std::format_string<Args...> fmt, Args&&... args) {
    detail::write_impl(Level::WARN, detail::current_tag, std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void error(std::format_string<Args...> fmt, Args&&... args) {
    detail::write_impl(Level::ERROR, detail::current_tag, std::format(fmt, std::forward<Args>(args)...));
}

// Simple message versions
inline void debug(std::string_view msg) {
    detail::write_impl(Level::DEBUG, detail::current_tag, msg);
}

inline void info(std::string_view msg) {
    detail::write_impl(Level::INFO, detail::current_tag, msg);
}

inline void warn(std::string_view msg) {
    detail::write_impl(Level::WARN, detail::current_tag, msg);
}

inline void error(std::string_view msg) {
    detail::write_impl(Level::ERROR, detail::current_tag, msg);
}

// ── Logging API: Explicit Tag with simple message ────────────────────────────────

template<std::string_view const& Tag>
inline void debug(std::string_view msg) {
    detail::write_impl(Level::DEBUG, Tag, msg);
}

template<std::string_view const& Tag>
inline void info(std::string_view msg) {
    detail::write_impl(Level::INFO, Tag, msg);
}

template<std::string_view const& Tag>
inline void warn(std::string_view msg) {
    detail::write_impl(Level::WARN, Tag, msg);
}

template<std::string_view const& Tag>
inline void error(std::string_view msg) {
    detail::write_impl(Level::ERROR, Tag, msg);
}

// ── Logging API: Explicit Tag with formatting ──────────────────────────────────

template<std::string_view const& Tag, typename... Args>
    requires(sizeof...(Args) > 0) // Require at least one arg to avoid ambiguity with simple string_view version
inline void debug(std::format_string<Args...> fmt, Args&&... args) {
    detail::write_impl(Level::DEBUG, Tag, std::format(fmt, std::forward<Args>(args)...));
}

template<std::string_view const& Tag, typename... Args>
    requires(sizeof...(Args) > 0)
inline void info(std::format_string<Args...> fmt, Args&&... args) {
    detail::write_impl(Level::INFO, Tag, std::format(fmt, std::forward<Args>(args)...));
}

template<std::string_view const& Tag, typename... Args>
    requires(sizeof...(Args) > 0)
inline void warn(std::format_string<Args...> fmt, Args&&... args) {
    detail::write_impl(Level::WARN, Tag, std::format(fmt, std::forward<Args>(args)...));
}

template<std::string_view const& Tag, typename... Args>
    requires(sizeof...(Args) > 0)
inline void error(std::format_string<Args...> fmt, Args&&... args) {
    detail::write_impl(Level::ERROR, Tag, std::format(fmt, std::forward<Args>(args)...));
}

// ── Per-translation-unit tag ──────────────────────────────────────────────────
// Use at file scope to tag all log calls in that TU:
//   static const log::TuTag LOG(log::tags::ROUTES);
struct TuTag {
    explicit TuTag(std::string_view tag) noexcept {
        detail::current_tag = tag;
    }
};

} // namespace log
