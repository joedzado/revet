// Auto-generated — do not edit
#pragma once
#include <string_view>
#include <format>
#include <revet/web.hpp>
#include <revet/upload.hpp>
#include "toggles.hpp"
#include "components.hpp"
#include "i18n.hpp"
#include "uploads.hpp"
#include "resources.hpp"

namespace urls {
    inline constexpr std::string_view index = "/";
    inline constexpr std::string_view about = "/about";
    inline constexpr std::string_view about_team = "/about/team";
    inline constexpr std::string_view api_v1_users = "/api/v1/users";
    inline constexpr std::string_view api_v2_items = "/api/v2/items";
    inline constexpr std::string_view chat = "/chat";
    inline constexpr std::string_view chat_send = "/chat/send";
    inline constexpr std::string_view chat_stream = "/chat/stream";
    inline constexpr std::string_view components = "/components";
    inline constexpr std::string_view contact = "/contact";
    inline constexpr std::string_view interactive = "/interactive";
    inline constexpr std::string_view interactive_search = "/interactive/search";
    inline constexpr std::string_view items = "/items";
    inline constexpr std::string_view live = "/live";
    inline constexpr std::string_view live_stream = "/live/stream";
    inline constexpr std::string_view quakes = "/quakes";
    inline constexpr std::string_view set_locale = "/set_locale";
    inline constexpr std::string_view site_init = "/site_init";
    inline constexpr std::string_view upload = "/upload";
    inline constexpr std::string_view users = "/users";
    inline constexpr std::string_view ws_echo = "/ws/echo";
    inline auto fonts__file_(std::string_view file) -> std::string { return std::format("/fonts/{}", sanitize::url_encode(file)); }
    inline auto items__id__edit_form(std::string_view id) -> std::string { return std::format("/items/{}/edit-form", sanitize::url_encode(id)); }
    inline auto users__id_(std::string_view id) -> std::string { return std::format("/users/{}", sanitize::url_encode(id)); }
    inline constexpr std::string_view compact = "/toggle_compact";
    inline constexpr std::string_view sb_open = "/toggle_sidebar";
    inline constexpr std::string_view dark_mode = "/toggle_theme";
}

namespace revet {
inline constexpr std::string_view version = "unknown";
} // namespace revet