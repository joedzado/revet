#include <revet/web.hpp>
#include <revet/csrf.hpp>
#include <revet/log.hpp>
#include "json_models.hpp"
#include "forms.hpp"

static const log::TuTag LOG(log::tags::ROUTES);

// Guards run before state injection, so we check the raw session cookie.
static auto route_guards() -> std::vector<web::Middleware> {
    return {
        [](const web::Request& req, web::Next next) -> web::Response {
            if (web::states::has<Session>(req.cookies))
                return web::redirect(urls::index);
            return next();
        }
    };
}

static auto render_page(const web::Request& req, const LoginForm& form,
                        std::string_view form_error = {}) -> web::Response {
    const auto error_banner = !form_error.empty()
        ? html::Node{html::div{styles::toast, styles::toast_error, form_error}}
        : html::Node{};

    auto field_group = [](const auto& field) -> html::Node {
        const bool has_error = !field.error.empty();
        return html::div{styles::field_group,
            html::label{styles::field_label, html::attr.for_(field.name),
                std::remove_cvref_t<decltype(field)>::field_type::label},
            html::input{
                html::attr.type(std::remove_cvref_t<decltype(field)>::field_type::input_type),
                html::attr.name(field.name),
                html::attr.value(field.value),
                has_error ? html::attr.class_("field-invalid") : html::attr.class_(""),
            },
            has_error
                ? html::Node{html::span{styles::field_error, field.error}}
                : html::Node{}
        };
    };

    const auto content = html::div{styles::container, styles::page_content,
        error_banner,
        html::h1{styles::page_title, "Sign in"},
        html::div{styles::form_card,
            html::form{html::attr.post(), html::attr.action(urls::login),
                web::csrf::field(req),
                field_group(form.username),
                field_group(form.password),
                html::div{styles::form_actions,
                    html::submit{styles::btn, "Sign in"}
                }
            }
        }
    };

    return layout_main(req, "Sign in — {{name}}", content);
}

static auto render_get(const web::Request& req) -> web::Response {
    return render_page(req, {});
}

static auto render_post(const web::Request& req) -> web::Response {
    LoginForm form;
    forms::populate(form, req.form);
    if (auto result = forms::validate(form); !result)
        return render_page(req, form);

    // Replace with a real DB lookup and password hash check (e.g. bcrypt).
    if (form.username.value != "admin" || form.password.value != "password") {
        log::warn("Failed login attempt for user: {}", form.username.value);
        return render_page(req, form, "Invalid username or password.");
    }

    log::info("Login: {}", form.username.value);
    return web::redirect(urls::index).state(Session{.username = form.username.value});
}
