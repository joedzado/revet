#pragma once

// JSON endpoints for passkey login, for native apps. Each function takes the request and a callable
// that opens a write handle, and returns the response, so a route file is a single line:
//
//   // app/routes/api/v1/auth/passkey/login/options.cpp
//   static auto render_post(const web::Request& req) -> web::Response {
//       return web::auth::passkeys::http::login_options(req, [] { return resources::main_db<db::Write>(); });
//   }
//   static auto api_guards() -> std::vector<web::Middleware> {            // public, but limited
//       return { web::rate_limit::by_ip(limiter) };
//   }
//
// Request and response bodies are JSON. Binary fields are base64url without padding.
//
//   register_options   POST  {display_name?}                          -> {challenge_id, options}
//                            Authorization: Bearer <access token> adds a passkey to that user;
//                            without it, finishing creates a new account.
//   register_finish    POST  {challenge_id, client_data_json, attestation_object, label?, device?}
//                                                                     -> {user_id, credential_id, new_user, tokens?}
//                            tokens (a signed-in session) are returned when the account is new.
//   login_options      POST  {}                                       -> {challenge_id, options}
//   login_finish       POST  {challenge_id, credential_id, client_data_json, authenticator_data,
//                             signature, user_handle?, device?}       -> {user_id, tokens}
//   list               GET   (guarded by tokens::guard())             -> {passkeys: [...]}
//   remove             POST  {credential_id} (guarded)                -> {removed: true}
//
// `options` is the WebAuthn JSON form the platform APIs accept. Errors are {"error": "<code>"}.

#include <memory>
#include <string>
#include <string_view>

#include "../json.hpp"
#include "../json_write.hpp"
#include "../log.hpp"
#include "../web.hpp"
#include "passkeys.hpp"
#include "tokens.hpp"

namespace web::auth::passkeys::http {

namespace detail {

using Failure = web::auth::passkeys::Failure;

inline auto json_response(std::string body, web::Status status = web::status::ok) -> web::Response {
    return web::Response{.body = std::move(body), .content_type = "application/json", .status = status};
}

// The request body as JSON, or an error response. Missing fields read as "".
class Body {
public:
    static auto parse(const web::Request& req) -> std::expected<Body, web::Response> {
        if (req.raw_body.empty()) {
            return std::unexpected(web::api::error(web::status::bad_request, "json_body_required"));
        }
        try {
            return Body(std::make_unique<revet::json::Document>(req.raw_body));
        } catch (const std::exception&) {
            return std::unexpected(web::api::error(web::status::bad_request, "bad_json"));
        }
    }
    [[nodiscard]] auto str(std::string_view key) -> std::string {
        try {
            return doc_->root().get(key, "");
        } catch (const std::exception&) {
            return {};
        }
    }

private:
    explicit Body(std::unique_ptr<revet::json::Document> d) : doc_(std::move(d)) {}
    std::unique_ptr<revet::json::Document> doc_;
};

inline auto tokens_json(revet::json::Writer& w, const tokens::TokenPair& t) -> void {
    w.key("tokens").object()
        .key("access_token").value(t.access_token)
        .key("refresh_token").value(t.refresh_token)
        .key("access_expires_at").value(t.access_expires_at)
        .key("refresh_expires_at").value(t.refresh_expires_at)
        .key("session_id").value(t.session_id)
        .end();
}

// Registration may say why it failed: it is the developer's own app on the other end.
inline auto registration_failure(const Failure& f) -> web::Response {
    switch (f.kind) {
    case Failure::Kind::bad_request: return web::api::error(web::status::bad_request, "bad_request");
    case Failure::Kind::challenge_invalid: return web::api::error(web::status::bad_request, "challenge_invalid");
    case Failure::Kind::credential_taken: return web::api::error(web::status::conflict, "credential_taken");
    case Failure::Kind::no_such_user: return web::api::error(web::status::unauthorized, "unauthorized");
    case Failure::Kind::verification_failed: {
        const auto reason = f.detail ? std::string(webauthn::error_name(*f.detail)) : std::string("unknown");
        log::warn("passkey registration refused: {}", reason);
        revet::json::Writer w;
        w.object().key("error").value("verification_failed").key("reason").value(reason).end();
        return json_response(std::move(w).str(), web::status::bad_request);
    }
    case Failure::Kind::unknown_credential:
    case Failure::Kind::user_mismatch: break;
    }
    return web::api::error(web::status::bad_request, "bad_request");
}

// Login says as little as it can: one code for every way the answer can be wrong.
inline auto login_failure(const Failure& f) -> web::Response {
    switch (f.kind) {
    case Failure::Kind::bad_request: return web::api::error(web::status::bad_request, "bad_request");
    case Failure::Kind::challenge_invalid: return web::api::error(web::status::bad_request, "challenge_invalid");
    default: break;
    }
    log::warn("passkey login refused: {}{}", static_cast<int>(f.kind),
              f.detail ? std::string(" ") + std::string(webauthn::error_name(*f.detail)) : std::string());
    return web::api::error(web::status::unauthorized, "login_failed");
}

// No Authorization header: nullopt, fine. One that does not verify: an error response.
inline auto optional_user(const web::Request& req) -> std::expected<std::optional<tokens::Principal>, web::Response> {
    const auto header = req.headers.get("authorization");
    if (header.empty()) return std::optional<tokens::Principal>{};
    if (!std::string_view(header).starts_with("Bearer ")) {
        return std::unexpected(web::api::error(web::status::unauthorized, "unauthorized"));
    }
    auto p = tokens::verify_access(std::string_view(header).substr(7));
    if (!p) return std::unexpected(web::api::error(web::status::unauthorized, "unauthorized"));
    return p;
}

} // namespace detail

template<typename Open>
auto register_options(const web::Request& req, Open open) -> web::Response {
    const auto who = detail::optional_user(req);
    if (!who) return who.error();
    std::string name = "user";
    if (!req.raw_body.empty()) {
        auto body = detail::Body::parse(req);
        if (!body) return body.error();
        if (auto n = body->str("display_name"); !n.empty() && n.size() <= 128) name = std::move(n);
    }
    auto h = open();
    const auto user = *who ? std::optional<std::string>((*who)->user_id) : std::nullopt;
    const auto opts = begin_registration(h, user, name);
    if (!opts) return detail::registration_failure(opts.error());
    revet::json::Writer w;
    w.object().key("challenge_id").value(opts->challenge_id).key("options").raw(opts->options_json).end();
    return detail::json_response(std::move(w).str());
}

template<typename Open>
auto register_finish(const web::Request& req, Open open) -> web::Response {
    auto body = detail::Body::parse(req);
    if (!body) return body.error();
    auto label = body->str("label");
    if (label.size() > 128) label.resize(128);
    auto h = open();
    const auto done = finish_registration(h, body->str("challenge_id"), body->str("client_data_json"), body->str("attestation_object"), label);
    if (!done) return detail::registration_failure(done.error());

    revet::json::Writer w;
    w.object().key("user_id").value(done->user_id).key("credential_id").value(done->credential_id).key("new_user").value(done->new_user);
    if (done->new_user) {
        auto device = body->str("device");
        if (device.empty()) device = label;
        detail::tokens_json(w, tokens::issue(h, done->user_id, device));
    }
    w.end();
    return detail::json_response(std::move(w).str());
}

template<typename Open>
auto login_options(const web::Request& /*req*/, Open open) -> web::Response {
    auto h = open();
    const auto opts = begin_login(h);
    revet::json::Writer w;
    w.object().key("challenge_id").value(opts.challenge_id).key("options").raw(opts.options_json).end();
    return detail::json_response(std::move(w).str());
}

template<typename Open>
auto login_finish(const web::Request& req, Open open) -> web::Response {
    auto body = detail::Body::parse(req);
    if (!body) return body.error();
    auto h = open();
    const auto in = finish_login(h, body->str("challenge_id"), body->str("credential_id"), body->str("client_data_json"),
                                 body->str("authenticator_data"), body->str("signature"), body->str("user_handle"));
    if (!in) return detail::login_failure(in.error());

    auto device = body->str("device");
    if (device.size() > 128) device.resize(128);
    revet::json::Writer w;
    w.object().key("user_id").value(in->user_id);
    detail::tokens_json(w, tokens::issue(h, in->user_id, device));
    w.end();
    return detail::json_response(std::move(w).str());
}

// Behind tokens::guard().
template<typename Open>
auto list(const web::Request& /*req*/, Open open) -> web::Response {
    const auto& who = web::api::principal<tokens::Principal>();
    auto h = open();
    revet::json::Writer w;
    w.object().key("passkeys").array();
    for (const auto& p : list_passkeys(h, who.user_id)) {
        w.object()
            .key("credential_id").value(p.credential_id)
            .key("label").value(p.label)
            .key("created_at").value(p.created_at)
            .key("last_used_at").value(p.last_used_at)
            .key("backup_eligible").value(p.backup_eligible)
            .key("backup_state").value(p.backup_state)
            .end();
    }
    w.end().end();
    return detail::json_response(std::move(w).str());
}

// Behind tokens::guard().
template<typename Open>
auto remove(const web::Request& req, Open open) -> web::Response {
    const auto& who = web::api::principal<tokens::Principal>();
    auto body = detail::Body::parse(req);
    if (!body) return body.error();
    auto h = open();
    switch (remove_passkey(h, who.user_id, body->str("credential_id"))) {
    case RemoveResult::removed: return detail::json_response(R"({"removed":true})");
    case RemoveResult::not_found: return web::api::error(web::status::not_found, "not_found");
    case RemoveResult::last_login_method: return web::api::error(web::status::conflict, "last_login_method");
    }
    return web::api::error(web::status::internal_error, "internal_error");
}

// ── App association files ────────────────────────────────────────────────────

// Serves /.well-known/apple-app-site-association. iOS fetches it to let the app use this site's
// passkeys (webcredentials). Returns 404 until config().apple_app_ids is set. No redirects, no auth.
inline auto apple_app_site_association(const web::Request& /*req*/) -> web::Response {
    const auto& ids = config().apple_app_ids;
    if (ids.empty()) return web::api::error(web::status::not_found, "not_found");
    revet::json::Writer w;
    w.object().key("webcredentials").object().key("apps").array();
    for (const auto& id : ids) w.value(id);
    w.end().end().end();
    return detail::json_response(std::move(w).str());
}

// Serves /.well-known/assetlinks.json. Android reads it to let the app use this site's passkeys.
// Returns 404 until config().android_apps is set.
inline auto asset_links(const web::Request& /*req*/) -> web::Response {
    const auto& apps = config().android_apps;
    if (apps.empty()) return web::api::error(web::status::not_found, "not_found");
    revet::json::Writer w;
    w.array();
    for (const auto& app : apps) {
        w.object().key("relation").array().value("delegate_permission/common.get_login_creds").end();
        w.key("target").object().key("namespace").value("android_app").key("package_name").value(app.package_name)
            .key("sha256_cert_fingerprints").array();
        for (const auto& fp : app.sha256_cert_fingerprints) w.value(fp);
        w.end().end().end();
    }
    w.end();
    return detail::json_response(std::move(w).str());
}

} // namespace web::auth::passkeys::http
