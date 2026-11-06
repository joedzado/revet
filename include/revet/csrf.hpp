#pragma once

#include "web.hpp"

#include <string>
#include <vector>

#include "crypto.hpp"

namespace web::csrf {

static constexpr std::string_view k_field_name = "csrf_token";
static constexpr std::string_view k_cookie_name = "_csrf";
static constexpr std::string_view k_header_name = "x-csrf-token";

inline auto generate_token() -> std::string {
    unsigned char buf[32];
    randombytes_buf(buf, sizeof(buf));
    std::vector<char> hex(sizeof(buf) * 2 + 1);
    sodium_bin2hex(hex.data(), hex.size(), buf, sizeof(buf));
    return std::string(hex.data());
}

struct CsrfToken {
    std::string value;
};

// Read from thread_local ctx. May return "" if called from a coroutine that resumed
// on a different I/O thread — use token_from(req) in that case.
inline auto current_token() -> std::string {
    auto& ctx = Context::current();
    if (ctx.states.contains(typeid(CsrfToken))) {
        return std::any_cast<CsrfToken>(ctx.states.at(typeid(CsrfToken))).value;
    }
    return "";
}

// Safe version: reads ctx first, falls back to the incoming request cookie.
// Use this inside async handlers or layouts called from coroutines.
inline auto token_from(const Request& req) -> std::string {
    auto tok = current_token();
    if (!tok.empty()) {
        return tok;
    }
    return web::crypto::decrypt(req.cookies.get(k_cookie_name));
}

inline auto field(const Request& req) -> html::Node {
    return html::input{html::attr.type("hidden"), html::attr.name(k_field_name), html::attr.value(token_from(req))};
}

// hx-headers attribute that injects the CSRF token into every htmx request on an element.
// Drop onto <body> to cover the whole page: html::body{ web::csrf::htmx_headers(req), ... }
inline auto htmx_headers(const Request& req) -> html::Node {
    const auto tok = token_from(req);
    if (tok.empty()) {
        return html::Node{};
    }
    return html::attr("hx-headers", std::format("{{\"{}\":\"{}\"}}", k_header_name, tok));
}

// Recover the plaintext CSRF token from an encrypted cookie value. Returns
// "" if decryption fails — which is exactly what we want a subdomain
// attacker, who lacks REVET_COOKIE_KEY, to see when they try to forge a
// cookie. The decryption is AEAD (XChaCha20-Poly1305) so tampering also
// fails closed.
inline auto unseal_cookie(std::string_view sealed) -> std::string {
    if (sealed.empty()) return "";
    return web::crypto::decrypt(sealed);
}

// Middleware that ensures a CSRF token exists in cookies and validates it
// for state-changing requests.
//
// Cookie format: AEAD(raw_token). The raw token is what's emitted into
// forms / hx-headers; the cookie holds the sealed (encrypted+authenticated)
// version. This means:
//   - A subdomain attacker without REVET_COOKIE_KEY can't forge a valid
//     cookie, defeating the classic double-submit-cookie subdomain bypass.
//   - Tampering with the cookie ciphertext fails AEAD verification → 403.
//   - The compare against the form/header is constant-time.
inline auto middleware(const Request& req, const Next& next) -> Response {
    const std::string sealed_in   = req.cookies.get(k_cookie_name);
    std::string       raw_token   = unseal_cookie(sealed_in);
    const bool        need_rotate = raw_token.empty();

    if (need_rotate) {
        raw_token = generate_token();
    }

    // Store the *raw* token in context — that's what gets rendered into
    // forms and request headers.
    auto& ctx = Context::current();
    ctx.states[typeid(CsrfToken)] = CsrfToken{raw_token};

    // Validate for state-changing methods.
    if (req.method == Method::POST || req.method == Method::PUT ||
        req.method == Method::PATCH || req.method == Method::DEL) {
        std::string provided = req.form.get(k_field_name);
        if (provided.empty()) {
            provided = req.headers.get(k_header_name);
        }

        // Two conditions to reject:
        //  1. We had no valid sealed cookie to begin with (need_rotate).
        //     The client could not possibly know a fresh-this-request token,
        //     so this is always a forgery.
        //  2. Provided token doesn't equal the unsealed raw token,
        //     constant-time compared.
        if (need_rotate || provided.empty() ||
            !web::crypto::secure_eq(provided, raw_token)) {
            log::warn("CSRF: token mismatch for {}", std::string(req.path));
            Response resp = to_response(std::string(status::forbidden.text));
            resp.status = status::forbidden;
            return resp;
        }
    }

    Response resp = next();

    // Set (or refresh) the sealed cookie if we minted a new token or the
    // existing cookie was unparseable.
    if (need_rotate) {
        resp.cookies[k_cookie_name] = web::crypto::encrypt(raw_token);
    }

    return resp;
}

} // namespace web::csrf
