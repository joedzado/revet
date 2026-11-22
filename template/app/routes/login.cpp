#include <revet/auth/passkeys.hpp>
#include <revet/csrf.hpp>
#include <revet/htmx.hpp>
#include <revet/log.hpp>
#include <revet/web.hpp>

#include "json_models.hpp"

namespace {
const log::TuTag LOG(log::tags::ROUTES);

// Signed-in visitors have nothing to do here. Guards run before state injection,
// so we check the raw session cookie.
auto route_guards() -> std::vector<web::Middleware> {
    return {[](const web::Request& req, const web::Next& next) -> web::Response {
        if (web::states::has<Session>(req.cookies)) {
            return web::redirect(urls::index);
        }
        return next();
    }};
}

auto render_get(const web::Request& req) -> web::Response {
    const auto content = html::div{
        styles::container,
        styles::page_content,
        html::attr("data-csrf", web::csrf::token_from(req)),
        html::h1{styles::page_title, "Sign in"},
        html::div{styles::form_card,
                  html::p{"Use the passkey stored on this device. No password to remember or leak."},
                  html::div{styles::form_actions, html::button{styles::btn, html::attr.type("button"),
                                                               html::attr("data-passkey", "signin"),
                                                               "Sign in with a passkey"}}},
        html::div{styles::form_card, styles::passkey_section,
                  html::h2{styles::page_subtitle, "New here?"},
                  html::div{styles::field_group,
                            html::label{styles::field_label, html::attr.for_("passkey-name"), "Your name"},
                            html::input{html::attr.type("text"), html::attr.id("passkey-name"), html::attr.name("name"),
                                        html::attr.autocomplete("name")}},
                  html::div{styles::form_actions,
                            html::button{styles::btn, styles::btn_secondary, html::attr.type("button"),
                                         html::attr("data-passkey", "signup"), "Create an account with a passkey"}}},
        html::div{styles::form_card, styles::passkey_section,
                  html::h2{styles::page_subtitle, "Or get a link by email"},
                  html::p{"No passkey on this device? We email you a link that signs you in, and creates your account "
                          "the first time."},
                  html::form{htmx::attr::post(urls::auth_magic_request), htmx::attr::target("#magic-status"),
                             htmx::attr::swap_outer_html(),
                             html::div{styles::field_group,
                                       html::label{styles::field_label, html::attr.for_("magic-email"), "Email address"},
                                       html::input{html::attr.type("email"), html::attr.id("magic-email"),
                                                   html::attr.name("email"), html::attr.autocomplete("email"),
                                                   html::attr("required", "")}},
                             html::div{styles::form_actions,
                                       html::submit{styles::btn, styles::btn_secondary, "Email me a link"}}},
                  html::div{html::attr.id("magic-status")}},
        html::p{html::attr.id("passkey-status"), styles::passkey_status, html::attr("aria-live", "polite")},
        scripts::passkey_tag()};
    return layout_main(req, "Sign in — {{name}}", content);
}
} // namespace
