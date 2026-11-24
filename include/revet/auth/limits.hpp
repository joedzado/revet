#pragma once

// Rate limits for the auth endpoints, ready to drop into api_guards() / route_guards().
//
//   static auto api_guards() -> std::vector<web::Middleware> { return web::auth::limits::anonymous(); }
//   static auto api_guards() -> std::vector<web::Middleware> { return web::auth::limits::signed_in(); }
//
// anonymous()  public endpoints (sign-in and sign-up steps, token refresh): limited per client address,
//              and per credential, session or account when the request body names one.
// signed_in()  endpoints behind an access token: the token guard, then a limit per user.
// magic_request()  "mail me a link" routes: per client, per recipient address, and for the app as a whole.
//
// Limits are per process and in memory (see rate_limit.hpp). Set REVET_TRUSTED_PROXIES behind a proxy,
// or every client shares the proxy's address.

#include <string>
#include <vector>

#include "../json.hpp"
#include "../mail.hpp"
#include "../rate_limit.hpp"
#include "../web.hpp"
#include "tokens.hpp"

namespace web::auth::limits {

struct Config {
    rate_limit::Rule per_ip{.burst = 20, .per_second = 1.0 / 3, .max_keys = 100'000}; // 20 at once, then one per 3 s
    rate_limit::Rule per_name{
        .burst = 10, .per_second = 1.0 / 30, .max_keys = 100'000}; // a credential or session id: 10, then one per 30 s
    rate_limit::Rule per_user{
        .burst = 60, .per_second = 1.0, .max_keys = 100'000}; // a signed-in user: 60, then one per second
    rate_limit::Rule per_email{
        .burst = 3, .per_second = 1.0 / 1200, .max_keys = 100'000}; // mail to one address: 3, then one per 20 min
    rate_limit::Rule all_mail{
        .burst = 60, .per_second = 0.5, .max_keys = 1}; // mail from this app: 60, then 30 a minute
};

// Read when the limiters are first built, so set it at startup.
inline auto config() -> Config& {
    static Config c;
    return c;
}

namespace detail {

struct Limiters {
    std::shared_ptr<rate_limit::Limiter> ip = rate_limit::Limiter::make(config().per_ip);
    std::shared_ptr<rate_limit::Limiter> name = rate_limit::Limiter::make(config().per_name);
    std::shared_ptr<rate_limit::Limiter> user = rate_limit::Limiter::make(config().per_user);
    std::shared_ptr<rate_limit::Limiter> email = rate_limit::Limiter::make(config().per_email);
    std::shared_ptr<rate_limit::Limiter> all_mail = rate_limit::Limiter::make(config().all_mail);
};

inline auto limiters() -> Limiters& {
    static Limiters l;
    return l;
}

// Fresh, empty buckets built from config(). Guards made before this keep the old ones. For tests.
inline void reset() {
    limiters() = Limiters{};
}

// A JSON body's credential_id, else refresh_token's session part. Empty when the body names neither,
// which leaves only the per-address limit.
inline auto body_key(const Request& req) -> std::string {
    if (req.raw_body.empty() || req.raw_body.size() > 64 * 1024) {
        return {};
    }
    try {
        revet::json::Document doc(req.raw_body);
        auto root = doc.root();
        if (auto id = root.get("credential_id", ""); !id.empty() && id.size() <= 1400) {
            return "cred:" + id;
        }
        if (auto rt = root.get("refresh_token", ""); !rt.empty()) {
            const auto dot = rt.find('.');
            if (dot != std::string::npos && dot <= 64) {
                return "sess:" + rt.substr(0, dot);
            }
        }
    } catch (const std::exception&) {
    }
    return {};
}

// The address a "mail me a link" request names, as the mail code will see it: from a JSON body
// {"email": ...} or a form field. Empty when there is none or it is not an address.
inline auto email_key(const Request& req) -> std::string {
    std::string raw;
    if (!req.raw_body.empty() && req.raw_body.size() <= 64 * 1024) {
        try {
            revet::json::Document doc(req.raw_body);
            raw = doc.root().get("email", "");
        } catch (const std::exception&) {
        }
    }
    if (raw.empty()) {
        raw = req.form.get("email");
    }
    const auto normal = mail::normalize_address(raw);
    return normal ? "email:" + *normal : std::string();
}

} // namespace detail

inline auto anonymous() -> std::vector<Middleware> {
    auto& l = detail::limiters();
    return {rate_limit::by_ip(l.ip), rate_limit::by_key(l.name, detail::body_key)};
}

// For "mail me a link" routes: the usual per-address limit, then a few mails per recipient, then a ceiling on
// all mail the app sends (so many addresses from many places cannot run up the SMTP bill or get the sender blocked).
// The recipient limit is checked before the global one, so one hammered address does not use up everybody's allowance.
inline auto magic_request() -> std::vector<Middleware> {
    auto& l = detail::limiters();
    return {rate_limit::by_ip(l.ip), rate_limit::by_key(l.email, detail::email_key),
            rate_limit::by_key(l.all_mail, [](const Request&) { return std::string("mail"); })};
}

inline auto signed_in() -> std::vector<Middleware> {
    auto& l = detail::limiters();
    return {tokens::guard(), rate_limit::by_key(l.user, [](const Request&) {
                return "user:" + web::api::principal<tokens::Principal>().user_id;
            })};
}

} // namespace web::auth::limits
