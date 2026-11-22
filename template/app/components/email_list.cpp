#include <revet/auth/users.hpp>
#include <revet/htmx.hpp>
#include <revet/web.hpp>
#include "resources.hpp"
#include "routes.hpp"
#include "styles.hpp"

static constexpr html::id EMAIL_LIST{"email-list"};

// The email addresses that can sign this user in, each with a Remove button. Carries its own id so htmx
// can swap it after a removal.
[[revet::export]]
html::Node email_list(std::string_view user_id) {
    auto db = resources::users_db();
    std::vector<web::auth::Identity> emails;
    for (auto& i : web::auth::identities_of(db, user_id)) {
        if (i.provider == "email") emails.push_back(std::move(i));
    }
    if (emails.empty()) {
        return html::div{EMAIL_LIST, styles::passkey_list, html::p{styles::passkey_meta, "No email address added yet."}};
    }
    return html::div{EMAIL_LIST, styles::passkey_list,
        html::list(emails, [](const auto& e) -> html::Node {
            return html::div{styles::passkey_row,
                html::div{html::div{styles::passkey_name, e.subject}},
                html::button{styles::btn, styles::btn_secondary, styles::btn_sm,
                    html::attr.type("button"),
                    htmx::attr::post(urls::account_emails_remove),
                    htmx::attr::vals(R"({"email":")" + e.subject + R"("})"),
                    htmx::attr::target(EMAIL_LIST),
                    htmx::attr::swap_outer_html(),
                    "Remove"}
            };
        })
    };
}
