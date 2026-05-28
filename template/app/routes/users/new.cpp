#include <revet/web.hpp>
#include <revet/csrf.hpp>
#include <revet/log.hpp>
#include "resources.hpp"
#include "queries.hpp"
#include "json_models.hpp"
#include "forms.hpp"

static const log::TuTag LOG(log::tags::ROUTES);

static auto render_page(const web::Request& req, const UserNewForm& form) -> web::Response {
    auto field_group = [](const auto& field, std::string_view placeholder) -> html::Node {
        const bool has_error = !field.error.empty();
        return html::div{styles::field_group,
            html::label{styles::field_label, html::attr.for_(field.name),
                std::remove_cvref_t<decltype(field)>::field_type::label},
            html::input{
                html::attr.type(std::remove_cvref_t<decltype(field)>::field_type::input_type),
                html::attr.name(field.name),
                html::attr.value(field.value),
                html::attr.placeholder(placeholder),
                has_error ? html::attr.class_("field-invalid") : html::attr.class_(""),
            },
            has_error
                ? html::Node{html::span{styles::field_error, field.error}}
                : html::Node{}
        };
    };

    const auto content = html::div{styles::container, styles::page_content,
        html::a{styles::back_link, html::attr.href(urls::users), "← Users"},
        html::h1{styles::page_title, "New User"},
        html::div{styles::form_card,
            html::form{html::attr.post(), html::attr.action(urls::users_new),
                html::attr.autocomplete("off"),
                web::csrf::field(req),
                field_group(form.name,  "Alice Smith"),
                field_group(form.email, "alice@example.com"),
                html::div{styles::form_actions,
                    html::submit{styles::btn, "Create User"},
                    html::a{styles::btn, styles::btn_secondary, html::attr.href(urls::users), "Cancel"}
                }
            }
        }
    };

    return layout_main(req, "New User — {{name}}", content);
}

static auto render_get(const web::Request& req) -> web::Response {
    return render_page(req, {});
}

static auto render_post(const web::Request& req) -> web::Task<web::Response> {
    UserNewForm form;
    forms::populate(form, req.form);
    if (auto result = forms::validate(form); !result) {
        co_return render_page(req, form);
    }

    auto db = resources::users_db<db::Write>();
    try {
        co_await queries::users::create(db, {.name = form.name.value, .email = form.email.value});
        log::info("Created user: {} <{}>", form.name.value, form.email.value);
    } catch (const std::exception& e) {
        log::warn("Create user failed: {}", e.what());
        form.email.error = "Email already exists.";
        co_return render_page(req, form);
    }

    co_return web::redirect(urls::users)
        .flash(Toast{.message = "User created successfully.", .kind = "success"});
}
