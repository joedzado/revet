#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>

namespace sanitize {

// Escapes HTML special characters to prevent XSS.
constexpr auto html_escape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size() * 6);
    for (const char c : s) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

// Returns the URL unchanged if it uses a safe scheme, otherwise returns "#".
// Blocks javascript:, data:, vbscript:, and other executable schemes.
inline auto safe_url(std::string_view url) -> std::string {
    const auto start = url.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        return "";
    }
    const auto trimmed = url.substr(start);

    if (trimmed.empty() || trimmed.starts_with('/') || trimmed.starts_with('#') || trimmed.starts_with('?') ||
        trimmed.starts_with("//")) {
        return std::string(url);
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string_view::npos) {
        return std::string(url); // Relative path like "users/1"
    }

    // Check scheme
    static constexpr std::array<std::string_view, 4> safe_schemes = {"http", "https", "mailto", "tel"};
    auto scheme = trimmed.substr(0, colon);
    auto scheme_matches = [](std::string_view s, std::string_view p) -> bool {
        if (s.size() != p.size()) {
            return false;
        }
        return std::ranges::equal(s, p, [](unsigned char a, unsigned char b) -> bool { return std::tolower(a) == b; });
    };

    if (std::ranges::any_of(safe_schemes, [&](std::string_view p) -> bool { return scheme_matches(scheme, p); })) {
        return std::string(url);
    }

    return "#";
}

inline auto url_decode(std::string_view str) -> std::string {
    std::string res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str.at(i) == '+') {
            res += ' ';
        } else if (str.at(i) == '%' && i + 2 < str.size()) {
            int v = 0;
            const auto hex = str.substr(i + 1, 2);
            auto [ptr, ec] = std::from_chars(std::to_address(hex.begin()), std::to_address(hex.end()), v, 16);
            if (ec != std::errc{} || ptr != std::to_address(hex.end())) {
                res += str.at(i);
            } else {
                res += static_cast<char>(v);
                i += 2;
            }
        } else {
            res += str.at(i);
        }
    }
    return res;
}

// Percent-decodes a path segment. Unlike url_decode, '+' stays '+' (it only means space in a query string).
inline auto url_decode_path(std::string_view str) -> std::string {
    std::string res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        int v = 0;
        if (str.at(i) == '%' && i + 3 <= str.size()) {
            const auto hex = str.substr(i + 1, 2);
            auto [ptr, ec] = std::from_chars(std::to_address(hex.begin()), std::to_address(hex.end()), v, 16);
            if (ec == std::errc{} && ptr == std::to_address(hex.end())) {
                res += static_cast<char>(v);
                i += 2;
                continue;
            }
        }
        res += str.at(i);
    }
    return res;
}

// Percent-encodes everything but RFC 3986 unreserved characters (A-Z a-z 0-9 - . _ ~),
// so the result is safe as a path segment or a query value.
inline auto url_encode(std::string_view str) -> std::string {
    static constexpr std::string_view hex = "0123456789ABCDEF";
    std::string res;
    res.reserve(str.size());
    for (const char ch : str) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0 || c == '-' || c == '.' || c == '_' || c == '~') {
            res += ch;
        } else {
            res += '%';
            res += hex.at(c >> 4);
            res += hex.at(c & 0xF);
        }
    }
    return res;
}

inline auto base64_encode(std::string_view in) -> std::string {
    static constexpr std::array<char, 64> lookup = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
        'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
        's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    uint32_t val = 0;
    int valb = -6;
    for (const unsigned char c : in) {
        val = (val << 8U) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(lookup.at((val >> static_cast<uint32_t>(valb)) & 0x3FU));
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(lookup.at(((val << 8U) >> static_cast<uint32_t>(valb + 8)) & 0x3FU));
    }
    while ((out.size() % 4) != 0U) {
        out.push_back('=');
    }
    return out;
}

inline auto base64_decode(std::string_view in) -> std::string {
    static const std::array<int, 256> T = []() -> std::array<int, 256> {
        std::array<int, 256> t{};
        t.fill(-1);
        const std::string_view l = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < l.size(); i++) {
            t.at(static_cast<unsigned char>(l.at(i))) = static_cast<int>(i);
        }
        return t;
    }();
    std::string out;
    out.reserve(in.size() * 3 / 4 + 1);
    uint32_t val = 0;
    int valb = -8;
    for (const unsigned char c : in) {
        if (T.at(c) == -1) {
            break;
        }
        val = (val << 6U) + static_cast<uint32_t>(T.at(c));
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> static_cast<uint32_t>(valb)) & 0xFFU));
            valb -= 8;
        }
    }
    return out;
}

} // namespace sanitize
