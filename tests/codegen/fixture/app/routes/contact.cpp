#include "forms.hpp"

static auto fgroup(std::string_view id, std::string_view lbl,
                   std::string_view val, std::string_view err,
                   bool is_textarea = false, std::string_view type = "text") -> html::Node {
    return html::div{styles::fgroup,
        html::label{html::attr.for_(id), lbl},
        is_textarea ?
            html::Node(html::textarea{html::attr.id(id), html::attr.name(id),
                           err.empty() ? html::Node{} : styles::has_err,
                           val}) :
            html::Node(html::input{html::attr.id(id), html::attr.name(id), html::attr.type(type),
                        html::attr.value(val),
                        err.empty() ? html::Node{} : styles::has_err}),
        err.empty() ? html::Node{} : html::div{styles::ferr, err}
    };
}

static auto render_form(const web::Request& req, const ContactForm& f, bool has_errors = false) -> web::Response {
    const auto error_alert = !has_errors ? html::Node{} :
        alert_err(html::strong{i18n::error_validation_failed} + html::Node{i18n::error_correct_below});

    const auto phone_val = f.phone ? f.phone->value : "";
    const auto phone_err = f.phone ? f.phone->error : "";

    const auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_contact_eyebrow},
            html::h1{i18n::page_contact_h1},
            html::p{i18n::page_contact_body},
        } +
        sec_label(i18n::section_rendered_form) +
        html::div{styles::form_wrap,
            error_alert,
            html::form{html::attr.action(urls::contact), html::attr.method("POST"),
                       html::attr.novalidate(),
                web::csrf::field(req),
                fgroup("name",    i18n::field_full_name,   f.name.value,    f.name.error)          +
                fgroup("email",   i18n::field_email_addr,  f.email.value,   f.email.error, false, "email") +
                fgroup("message", i18n::field_message,     f.message.value, f.message.error, true) +
                fgroup("phone",   i18n::field_phone,       phone_val,       phone_err, false, "tel") +
                html::button{html::attr.type("submit"), styles::btn, i18n::btn_send_message},
            },
        };

    return layout_page(req, i18n::page_contact_h1, urls::contact, content);
}

static auto render_get(const web::Request& req) -> web::Response {
    return render_form(req, ContactForm{});
}

static auto render_post(const web::Request& req) -> web::Response {
    ContactForm f;
    forms::populate(f, req.form);
    if (!forms::validate(f))
        return render_form(req, f, true);

    const auto content =
        html::div{styles::hero,
            html::h1{i18n::page_sent_h1},
            html::p{i18n::page_sent_body, ", ", html::strong{f.name.value}, ". ", i18n::page_sent_received},
        } +
        alert_ok(
            html::strong{i18n::alert_form_ok} +
            html::p{styles::alert_detail, i18n::label_email, " ", f.email.value}
        ) +
        html::a{html::attr.href(urls::contact), styles::btn, styles::btn_ghost, i18n::btn_send_another};

    return layout_page(req, i18n::page_sent_h1, urls::contact, content);
}
