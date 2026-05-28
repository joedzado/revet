
static auto render_get(const auto& req) {
    const auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_about_eyebrow},
            html::h1{i18n::page_about_h1},
            html::p{i18n::page_about_body},
        } +
        sec_label(i18n::section_vision) +
        html::p{i18n::vision_body, " ", html::strong{i18n::vision_dx}, "."} +
        html::div{styles::cards,
            card("01", i18n::card_no_build_step,  i18n::card_no_build_step_body),
            card("02", i18n::card_type_safe_dsl,  i18n::card_type_safe_dsl_body),
            card("03", i18n::card_zero_deps,       i18n::card_zero_deps_body),
        } +
        ui::tabs("abt", {
            {"style", i18n::tab_code_style,
             html::fragment(
                 html::p{styles::tab_intro, i18n::tab_code_style_intro},
                 html::pre{
R"cpp(// site/about.cpp

static auto render_get(const auto& req) {
    auto content = html::div{
        html::h1{"Hello from C++"}
    };
    return layout_page(req, i18n::page_about_h1, urls::about, content);
}
)cpp"             })},
            {"state", i18n::tab_state_mgmt,
             html::fragment(
                 html::p{styles::tab_intro, i18n::tab_state_intro},
                 html::pre{
R"cpp(// states/session.hpp
struct [[revet::state]] UserSession { std::string user_id; };
struct [[revet::state]] Notice      { std::string message; };

// Write — serialized into encrypted, signed cookies
auto s = UserSession{ .user_id = "123" };
return res.state(s);
auto n = Notice{ .message = "Profile updated." };
return res.flash(n);

// Read — deserialized from cookie on the next request
auto session = web::State <UserSession>::current();  // typed state
auto notice  = web::Flash <Notice>::current();        // cleared after one read
if (session) use(session->user_id);)cpp"
             })},
        });

    return layout_page(req, i18n::page_about_h1, urls::about, content);
}
