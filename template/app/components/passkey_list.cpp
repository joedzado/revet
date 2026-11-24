#include "resources.hpp"

#include "styles.hpp"

#include "routes.hpp"

#include <chrono>
#include <format>
#include <revet/auth/passkeys.hpp>
#include <revet/htmx.hpp>
#include <revet/web.hpp>

static constexpr html::id PASSKEY_LIST{"passkey-list"};

static auto date_text(long long unix_seconds) -> std::string {
    if (unix_seconds <= 0) {
        return "never";
    }
    return std::format("{:%Y-%m-%d}", std::chrono::sys_seconds{std::chrono::seconds{unix_seconds}});
}

// A user's passkeys with a Remove button each. Rendered on first load, after adding, and after removing,
// so it carries its own id for htmx to swap.
[[revet::export]]
html::Node passkey_list(std::string_view user_id) {
    auto db = resources::users_db();
    const auto passkeys = web::auth::passkeys::list_passkeys(db, user_id);
    return html::div{
        PASSKEY_LIST, styles::passkey_list, html::list(passkeys, [](const auto& p) -> html::Node {
            return html::div{
                styles::passkey_row,
                html::div{html::div{styles::passkey_name, p.label.empty() ? std::string("Passkey") : p.label},
                          html::div{styles::passkey_meta, "Added " + date_text(p.created_at) + " · last used " +
                                                              date_text(p.last_used_at) +
                                                              (p.backup_state ? " · synced" : "")}},
                html::button{styles::btn, styles::btn_secondary, styles::btn_sm, html::attr.type("button"),
                             htmx::attr::post(urls::account_passkeys_remove),
                             htmx::attr::vals(R"({"credential_id":")" + p.credential_id + R"("})"),
                             htmx::attr::target(PASSKEY_LIST), htmx::attr::swap_outer_html(), "Remove"}};
        })};
}
