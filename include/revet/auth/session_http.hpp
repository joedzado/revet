#pragma once

// JSON endpoints that keep a native app signed in, and let the user leave.
//
//   refresh        POST {refresh_token}        -> {tokens}                        public
//   logout         POST                        -> {"ok":true}                     behind tokens::guard()
//   logout_all     POST                        -> {"ok":true,"signed_out":<n>}    behind tokens::guard()
//   devices        GET                         -> {devices:[...]}                 behind tokens::guard()
//   delete_account POST                        -> {"ok":true}                     behind tokens::guard()
//
// Like passkeys_http.hpp, each takes the request and a callable that opens a write handle:
//
//   static auto render_post(const web::Request& req) -> web::Response {
//       return web::auth::session::http::refresh(req, [] { return resources::main_db<db::Write>(); });
//   }
//   static auto api_guards() -> std::vector<web::Middleware> { return web::auth::limits::anonymous(); }

#include <string>
#include <string_view>

#include "../json.hpp"
#include "../json_write.hpp"
#include "../web.hpp"
#include "tokens.hpp"
#include "users.hpp"

namespace web::auth::session::http {

namespace detail {

inline auto json_ok(std::string body) -> web::Response {
    return web::Response{.body = std::move(body), .content_type = "application/json"};
}

} // namespace detail

// Trade a refresh token for a new pair. The old refresh token stops working. Every refusal is a 401
// with a code the app can act on: sign in again.
template<typename Open>
auto refresh(const web::Request& req, Open open) -> web::Response {
    if (req.raw_body.empty()) {
        return web::api::error(web::status::bad_request, "json_body_required");
    }
    std::string token;
    try {
        revet::json::Document doc(req.raw_body);
        token = doc.root().get("refresh_token", "");
    } catch (const std::exception&) {
        return web::api::error(web::status::bad_request, "bad_json");
    }
    if (token.empty()) {
        return web::api::error(web::status::bad_request, "bad_request");
    }

    auto h = open();
    const auto next = tokens::refresh(h, token);
    if (!next) {
        // An app should send the user back to sign-in on any of these; `session_revoked` also
        // means someone else may have held a copy of the old token.
        using E = tokens::RefreshError;
        const char* code = next.error() == E::expired ? "session_expired"
                           : (next.error() == E::revoked || next.error() == E::reuse_detected)
                               ? "session_revoked"
                               : "invalid_refresh_token";
        return web::api::error(web::status::unauthorized, code);
    }
    revet::json::Writer w;
    w.object()
        .key("tokens")
        .object()
        .key("access_token")
        .value(next->access_token)
        .key("refresh_token")
        .value(next->refresh_token)
        .key("access_expires_at")
        .value(next->access_expires_at)
        .key("refresh_expires_at")
        .value(next->refresh_expires_at)
        .key("session_id")
        .value(next->session_id)
        .end()
        .end();
    return detail::json_ok(std::move(w).str());
}

template<typename Open>
auto logout(const web::Request& /*req*/, Open open) -> web::Response {
    const auto& who = web::api::principal<tokens::Principal>();
    auto h = open();
    tokens::revoke_session(h, who.user_id, who.session_id);
    return detail::json_ok(R"({"ok":true})");
}

template<typename Open>
auto logout_all(const web::Request& /*req*/, Open open) -> web::Response {
    const auto& who = web::api::principal<tokens::Principal>();
    auto h = open();
    const auto n = tokens::revoke_all(h, who.user_id);
    revet::json::Writer w;
    w.object().key("ok").value(true).key("signed_out").value(static_cast<long long>(n)).end();
    return detail::json_ok(std::move(w).str());
}

template<typename Open>
auto devices(const web::Request& /*req*/, Open open) -> web::Response {
    const auto& who = web::api::principal<tokens::Principal>();
    auto h = open();
    revet::json::Writer w;
    w.object().key("devices").array();
    for (const auto& d : tokens::list_devices(h, who.user_id)) {
        w.object()
            .key("session_id")
            .value(d.session_id)
            .key("label")
            .value(d.label)
            .key("created_at")
            .value(d.created_at)
            .key("last_used_at")
            .value(d.last_used_at)
            .key("current")
            .value(d.session_id == who.session_id)
            .end();
    }
    w.end().end();
    return detail::json_ok(std::move(w).str());
}

// Deletes the account and everything that belongs to it (identities, sessions, passkeys). Final.
template<typename Open>
auto delete_account(const web::Request& /*req*/, Open open) -> web::Response {
    const auto& who = web::api::principal<tokens::Principal>();
    auto h = open();
    if (!delete_user(h, who.user_id)) {
        return web::api::error(web::status::not_found, "not_found");
    }
    return detail::json_ok(R"({"ok":true})");
}

} // namespace web::auth::session::http
