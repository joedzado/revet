#pragma once

// JSON endpoints for magic-link sign-in, for native apps.
//
//   request  POST {email}              -> {"ok":true}                       public, rate limited
//            With Authorization: Bearer <access token> the link adds the address to that user.
//   verify   POST {token, device?}     -> {user_id, new_user, tokens}       public
//            For an add-address link: {user_id, linked: true}, no tokens.
//
// The app receives the link (a universal link / app link on /auth/magic), takes `token` from it and POSTs
// it to `verify`. A browser lands on a page with a button instead (see the template), so a mail scanner
// that opens every link cannot use one up.
//
//   static auto render_post(const web::Request& req) -> web::Response {
//       return web::auth::magic::http::request(req, [] { return resources::main_db<db::Write>(); });
//   }
//   static auto api_guards() -> std::vector<web::Middleware> { return web::auth::limits::magic_request(); }

#include <string>
#include <string_view>

#include "../json.hpp"
#include "../json_write.hpp"
#include "../web.hpp"
#include "magic.hpp"
#include "tokens.hpp"

namespace web::auth::magic::http {

namespace detail {

inline auto json_ok(std::string body) -> web::Response {
    return web::Response{.body = std::move(body), .content_type = "application/json"};
}

// The JSON body's string fields, or an error response.
inline auto fields(const web::Request& req, std::initializer_list<std::string_view> names)
    -> std::expected<std::vector<std::string>, web::Response> {
    if (req.raw_body.empty()) {
        return std::unexpected(web::api::error(web::status::bad_request, "json_body_required"));
    }
    try {
        revet::json::Document doc(req.raw_body);
        std::vector<std::string> out;
        for (auto n : names) {
            out.push_back(doc.root().get(n, ""));
        }
        return out;
    } catch (const std::exception&) {
        return std::unexpected(web::api::error(web::status::bad_request, "bad_json"));
    }
}

} // namespace detail

template<typename Open>
auto request(const web::Request& req, Open open) -> web::Response {
    auto f = detail::fields(req, {"email"});
    if (!f) {
        return f.error();
    }

    Purpose purpose = Purpose::sign_in;
    std::string user_id;
    if (const auto header = req.headers.get("authorization"); !header.empty()) {
        // A token that does not verify is an error, not a quiet downgrade to creating a new account.
        const auto who = std::string_view(header).starts_with("Bearer ")
                             ? tokens::verify_access(std::string_view(header).substr(7))
                             : std::nullopt;
        if (!who) {
            return web::api::error(web::status::unauthorized, "unauthorized");
        }
        purpose = Purpose::link;
        user_id = who->user_id;
    }

    auto h = open();
    switch (magic::request(h, (*f)[0], purpose, user_id)) {
        case RequestResult::invalid_email:
            return web::api::error(web::status::bad_request, "invalid_email");
        case RequestResult::no_such_user:
            return web::api::error(web::status::unauthorized, "unauthorized");
        case RequestResult::sent:
        case RequestResult::throttled:
            break; // the same answer, so the endpoint tells nobody which addresses exist or how often they were asked
    }
    return detail::json_ok(R"({"ok":true})");
}

template<typename Open>
auto verify(const web::Request& req, Open open) -> web::Response {
    auto f = detail::fields(req, {"token", "device"});
    if (!f) {
        return f.error();
    }
    auto device = (*f)[1];
    if (device.size() > 128) {
        device.resize(128);
    }

    auto h = open();
    const auto done = magic::complete(h, (*f)[0]);
    if (!done) {
        return done.error() == CompleteError::email_taken ? web::api::error(web::status::conflict, "email_taken")
                                                          : web::api::error(web::status::unauthorized, "invalid_link");
    }
    revet::json::Writer w;
    w.object().key("user_id").value(done->user_id);
    if (done->linked) {
        w.key("linked").value(true);
    } else {
        const auto t = tokens::issue(h, done->user_id, device);
        w.key("new_user")
            .value(done->new_user)
            .key("tokens")
            .object()
            .key("access_token")
            .value(t.access_token)
            .key("refresh_token")
            .value(t.refresh_token)
            .key("access_expires_at")
            .value(t.access_expires_at)
            .key("refresh_expires_at")
            .value(t.refresh_expires_at)
            .key("session_id")
            .value(t.session_id)
            .end();
    }
    w.end();
    return detail::json_ok(std::move(w).str());
}

} // namespace web::auth::magic::http
