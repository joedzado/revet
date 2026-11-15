// Auto-generated — do not edit
#pragma once
#include <revet/web.hpp>
#include <revet/sanitize.hpp>
#include <revet/crypto.hpp>
#include "models.hpp"
#include <string>
#include <cstring>

namespace web::states {

template<typename T> struct Traits;
template<typename T> std::string pack(const T& val);
template<typename T> T unpack(std::string_view);
template<typename T> std::string pack_plain(const T& val);
template<typename T> T unpack_plain(std::string_view);

template<> struct Traits<Cart> {
    static constexpr std::string_view name = "Cart";
};

inline std::string pack_plain_Cart(const Cart& s) {
    std::string out;
    out.append((char*)&s.count, sizeof(s.count));
    { uint32_t len = (uint32_t)s.coupon.size(); out.append((char*)&len, 4); out.append(s.coupon); }
    return sanitize::base64_encode(out);
}

template<> inline std::string pack<Cart>(const Cart& s) {
    return crypto::encrypt(pack_plain_Cart(s));
}

inline Cart unpack_plain_Cart(std::string_view base64) {
    std::string raw = sanitize::base64_decode(base64);
    Cart out{};
    if (raw.empty()) return out;
    const char* p = raw.data();
    const char* end = p + raw.size();
    if (p + sizeof(out.count) <= end) { memcpy(&out.count, p, sizeof(out.count)); p += sizeof(out.count); }
    if (p + 4 <= end) { uint32_t len; memcpy(&len, p, 4); p += 4; if (p + len <= end) { out.coupon = std::string(p, len); p += len; } }
    return out;
}

inline Cart unpack_Cart(std::string_view encrypted) {
    std::string decrypted = crypto::decrypt(encrypted);
    return decrypted.empty() ? Cart{} : unpack_plain_Cart(decrypted);
}

template<> inline Cart unpack<Cart>(std::string_view encrypted) {
    return unpack_Cart(encrypted);
}

template<> inline std::string pack_plain<Cart>(const Cart& s) {
    return pack_plain_Cart(s);
}

template<> inline Cart unpack_plain<Cart>(std::string_view base64) {
    return unpack_plain_Cart(base64);
}

template<> struct Traits<Notice> {
    static constexpr std::string_view name = "Notice";
};

inline std::string pack_plain_Notice(const Notice& s) {
    std::string out;
    { uint32_t len = (uint32_t)s.message.size(); out.append((char*)&len, 4); out.append(s.message); }
    return sanitize::base64_encode(out);
}

template<> inline std::string pack<Notice>(const Notice& s) {
    return crypto::encrypt(pack_plain_Notice(s));
}

inline Notice unpack_plain_Notice(std::string_view base64) {
    std::string raw = sanitize::base64_decode(base64);
    Notice out{};
    if (raw.empty()) return out;
    const char* p = raw.data();
    const char* end = p + raw.size();
    if (p + 4 <= end) { uint32_t len; memcpy(&len, p, 4); p += 4; if (p + len <= end) { out.message = std::string(p, len); p += len; } }
    return out;
}

inline Notice unpack_Notice(std::string_view encrypted) {
    std::string decrypted = crypto::decrypt(encrypted);
    return decrypted.empty() ? Notice{} : unpack_plain_Notice(decrypted);
}

template<> inline Notice unpack<Notice>(std::string_view encrypted) {
    return unpack_Notice(encrypted);
}

template<> inline std::string pack_plain<Notice>(const Notice& s) {
    return pack_plain_Notice(s);
}

template<> inline Notice unpack_plain<Notice>(std::string_view base64) {
    return unpack_plain_Notice(base64);
}


// Type-safe cookie helpers
inline Cart get_Cart_from_cookies(const web::ParamMap& cookies) {
    return cookies.data.count("_cw_Cart") ? unpack<Cart>(cookies.data.at("_cw_Cart")) : Cart{};
}
inline Notice get_Notice_from_cookies(const web::ParamMap& cookies) {
    return cookies.data.count("_cw_Notice") ? unpack<Notice>(cookies.data.at("_cw_Notice")) : Notice{};
}

// has<T>: check if a state cookie is present (for use in route_guards before state injection)
template<typename T>
[[nodiscard]] inline bool has(const web::ParamMap& cookies) {
    return !cookies.get(std::string(cookie_prefix).append(Traits<T>::name)).empty();
}
} // namespace web::states