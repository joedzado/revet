#include "../../types/shared.hpp"
#include "apis.hpp"
#include "forms.hpp"
#include "i18n.hpp"
#include "models.hpp"
#include "nested.hpp"
#include "resources.hpp"
#include "routes.hpp"
#include "styles.hpp"
#include "users_data.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <random>
#include <revet/csrf.hpp>
#include <revet/htmx.hpp>
#include <revet/log.hpp>
#include <revet/ui.hpp>
#include <revet/upload.hpp>
#include <revet/websocket.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "htmx_asset.hpp"
#include "optimistic_asset.hpp"
#include "scripts.hpp"
#include "server.hpp"  // revet.toml max_body, applied at static init

namespace route_index {
namespace log = web::log; // ::log is the math function on glibc

static auto render_get(const auto& req) {
    const auto features = card_grid(
        card("routing",    i18n::feat_routing_title, i18n::feat_routing_body) +
        card("forms",      i18n::feat_forms_title,   i18n::feat_forms_body) +
        card("htmx",       i18n::feat_htmx_title,    i18n::feat_htmx_body) +
        card("security",   i18n::feat_security_title, i18n::feat_security_body) +
        card("middleware", i18n::feat_guards_title,   i18n::feat_guards_body) +
        card("logging",    i18n::feat_logging_title,  i18n::feat_logging_body)
    );

    static constexpr auto SNIPPET_ROUTING =
"// site/users/[id].cpp  ->  GET /users/:id\n"
"#include <revet/web.hpp>\n"
"\n"
"static auto render_get(const auto& req) {\n"
"    const auto id = req.params.get(\"id\");\n"
"    return html::div{\n"
"        html::h1{\"User \" + id},\n"
"        html::p{\"Found via parameterized route.\"},\n"
"    };\n"
"}";

    static constexpr auto SNIPPET_FORMS =
"// forms/contact.hpp\n"
"struct [[revet::form]] ContactForm {\n"
"    revet::Field<revet::FullName, validate::required_t,\n"
"                                  validate::max_len_t<100>> name;\n"
"    revet::Field<revet::Email,    validate::required_t,\n"
"                                  validate::max_len_t<254>> email;\n"
"    revet::Field<revet::Textarea, validate::required_t,\n"
"                                  validate::min_len_t<10>,\n"
"                                  validate::max_len_t<1000>> message;\n"
"    std::optional<revet::Field<revet::Tel>> phone;\n"
"};\n"
"\n"
"// In the handler (populate/validate are generated):\n"
"ContactForm f;\n"
"populate(f, req.form);   // fills fields from POST body\n"
"auto result = validate(f); // sets .error on each field";

    static constexpr auto SNIPPET_GUARD =
"// site/admin/dashboard.cpp  --  per-route guard\n"
"static auto route_" "guards() -> std::vector<web::Middleware> {\n"
"    return {\n"
"        [](const web::Request& req, web::Next next) -> web::Response {\n"
"            if (req.cookies.get(\"session\").empty())\n"
"                return web::redirect(urls::login);\n"
"            return next();\n"
"        }\n"
"    };\n"
"}\n"
"\n"
"static auto render_get(const auto& req) {\n"
"    return html::div{ html::h1{\"Dashboard\"} };\n"
"}";

    static constexpr auto SNIPPET_MW =
"// main.cpp  --  global middleware + startup\n"
"web::use([](const web::Request& req, const web::Next& next) {\n"
"    log::info(\"{} {}\", req.method, req.path);\n"
"    auto res = next();\n"
"    res.headers[\"X-Powered-By\"] = \"revet/0.1\";\n"
"    return res;\n"
"});\n"
"\n"
"web::listen(all_routes);   // port via --port flag or ServerConfig";

    static constexpr auto HERO_SNIPPET =
"// site/index.cpp  ->  GET /\n"
"static auto render_get(const auto& req) {\n"
"    return html::div{\n"
"        html::h1{\"C++ web, the modern way.\"},\n"
"        html::p{\"Server-rendered. Type-safe. Zero JS.\"},\n"
"    };\n"
"}";

    const auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_home_eyebrow},
            html::h1{i18n::page_home_h1},
            html::p{i18n::page_home_body, " ", html::code{"C++23"}, ". ", i18n::page_home_body_suffix},
            html::div{styles::hero_code,
                html::div{styles::hero_code_bar,
                    html::span{styles::hero_code_dot},
                    html::span{styles::hero_code_dot},
                    html::span{styles::hero_code_dot},
                    " site/index.cpp → GET /",
                },
                html::pre{HERO_SNIPPET},
            },
            html::div{styles::hero_btns,
                html::a{html::attr.href(urls::contact),    styles::btn, styles::btn_solid, i18n::btn_try_form},
                html::a{html::attr.href(urls::users),      styles::btn, styles::btn_ghost, i18n::btn_browse_users},
                html::a{html::attr.href(urls::components), styles::btn, styles::btn_ghost, i18n::btn_ui_components},
            },
        } +
        html::div{styles::stat_strip,
            html::div{styles::stat,
                html::span{styles::stat_n, "0"},
                html::span{styles::stat_l, i18n::stat_lines_js},
            },
            html::div{styles::stat,
                html::span{styles::stat_n, "~2k"},
                html::span{styles::stat_l, i18n::stat_lines_core},
            },
            html::div{styles::stat,
                html::span{styles::stat_n, "C++23"},
                html::span{styles::stat_l, i18n::stat_standard},
            },
            html::div{styles::stat,
                html::span{styles::stat_n, "∞"},
                html::span{styles::stat_l, i18n::stat_checks},
            },
        } +
        sec_label(i18n::section_features) +
        features +
        html::h2{i18n::section_quick_look} +
        html::p{
            i18n::section_quick_look_body, " ",
            html::code{"html::"}, " types that compose into valid HTML at compile time.",
        } +
        ui::tabs("ql", {
            {"routing",    i18n::tab_routing,
             html::fragment(html::p{styles::tab_intro, i18n::tab_routing_intro},
                            html::pre{SNIPPET_ROUTING})},
            {"forms",      i18n::tab_forms,
             html::fragment(html::p{styles::tab_intro, i18n::tab_forms_intro},
                            html::pre{SNIPPET_FORMS})},
            {"guards",     i18n::tab_guards,
             html::fragment(html::p{styles::tab_intro, i18n::tab_guards_intro},
                            html::pre{SNIPPET_GUARD})},
            {"middleware", i18n::tab_middleware,
             html::fragment(html::p{styles::tab_intro, i18n::tab_middleware_intro},
                            html::pre{SNIPPET_MW})},
        });

    return layout_page(req, i18n::page_home_h1, urls::index, content);
}
}

namespace route_about {
namespace log = web::log; // ::log is the math function on glibc

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
}

namespace route_about_team {
namespace log = web::log; // ::log is the math function on glibc

static auto render_get(const auto& req) {
    auto make_card = [](std::string_view initials, std::string_view name) {
        return html::div{styles::team_card,
            html::div{styles::team_avatar, initials},
            html::div{styles::team_name,   name},
        };
    };

    auto cards =
        make_card("NP", "Nora Padding")  +
        make_card("RV", "Rex Vec")       +
        make_card("Z",  "nullptr");

    auto content =
        breadcrumb(urls::about, i18n::nav_about) +
        html::div{styles::hero,
            html::h1{i18n::page_team_h1},
            html::p{i18n::page_team_body},
        } +
        html::div{styles::stat_strip,
            html::div{styles::stat, html::span{styles::stat_n, "0"},   html::span{styles::stat_l, i18n::stat_runtime_deps}},
            html::div{styles::stat, html::span{styles::stat_n, "~2k"}, html::span{styles::stat_l, i18n::stat_lines_core}},
            html::div{styles::stat, html::span{styles::stat_n, "∞"},   html::span{styles::stat_l, i18n::stat_checks}},
        } +
        sec_label(i18n::section_contributors) +
        html::div{styles::team_grid, std::move(cards)} +
        html::h2{i18n::h2_getting_involved} +
        html::p{i18n::getting_involved_body} +
        ui::toggle("getting-started", i18n::toggle_dev_commands,
            html::pre{
R"sh(revet dev      # start the server
./ctest --test-dir build     # build with ASAN + run all tests
./scripts/install.sh  # deploy to ~/.revet/current/  (requires sudo))sh"
            }
        );

    return layout_page(req, i18n::page_team_h1, urls::about_team, content);
}
}

namespace route_api_v1_users {
namespace log = web::log; // ::log is the math function on glibc

// GET /api/v1/users — return all users as a JSON array
static auto render_get(const web::Request& /*req*/) -> web::Response {
    std::string out = "[";
    bool first = true;
    for (const auto& [id, info] : USERS) {
        const auto& [name, email] = info;
        if (!first) out += ",";
        first = false;
        out += revet::json::to_json<User>(User{.id = id, .name = name, .email = email});
    }
    out += "]";
    return { .body = std::move(out), .content_type = "application/json" };
}

// POST /api/v1/users — CreateUser body → User response (typed model param)
// Framework validates Content-Type, body presence, and required fields before calling this.
static auto render_post(const CreateUser& body) -> User {
    auto id = std::to_string(USERS.size() + 1);
    return User{ .id = id, .name = body.name, .email = body.email, .role = body.role };
}

static auto api_guards() -> std::vector<web::Middleware> {
    return { web::api::bearer([](std::string_view tok) {
        return tok == "demo-key";
    })};
}
}

namespace route_api_v2_items {
namespace log = web::log; // ::log is the math function on glibc
static auto render_get(const web::Request& req) -> web::Task<std::vector<Item>> {
    co_return std::vector<Item>{};
}
static auto render_put(const shop::catalog::Item& body) -> web::Task<Item> { co_return body; }
static auto render_delete(const web::Request& req) -> web::Response { return {}; }
static auto render_patch(const Item& body) -> web::Response { return {}; }
}

namespace route_chat {
namespace log = web::log; // ::log is the math function on glibc

static constexpr html::id k_feed{"chat-messages"};

static const auto s_window = html::css_class({
    .max_width = "38rem",
    .margin = "0 auto",
    .overflow = "hidden",
    .border = "1px solid var(--border)",
    .border_radius = "var(--r)",
    .background = "var(--surface)",
});

static const auto s_feed = html::css_class({
    .display = "flex",
    .height = "22rem",
    .padding = "1rem",
    .overflow_y = "auto",
    .flex_direction = "column",
    .gap = ".1rem",
});

static const auto s_bar = html::css_class({
    .display = "flex",
    .padding = ".75rem 1rem",
    .align_items = "center",
    .gap = ".5rem",
    .background = "var(--surface)",
}).extra("border-top:1px solid var(--border)");

static const auto s_alice_row = html::css_class({
    .display = "flex",
    .margin = ".3rem 0",
    .align_items = "flex-start",
    .gap = ".5rem",
}).extra("animation:fadeIn .2s");

static const auto s_alice_avatar = html::css_class({
    .display = "flex",
    .width = "1.75rem",
    .height = "1.75rem",
    .flex_shrink = "0",
    .justify_content = "center",
    .align_items = "center",
    .border_radius = "50%",
    .background = "var(--muted)",
    .font_size = ".7rem",
    .font_weight = "700",
}).extra("color:var(--surface)");

static const auto s_alice_bubble = html::css_class({
    .max_width = "72%",
    .padding = ".5rem .875rem",
    .border = "1px solid var(--border)",
    .border_radius = ".25rem 1rem 1rem 1rem",
    .background = "var(--surface)",
    .font_size = ".875rem",
    .line_height = "1.4",
});

static const auto s_snippet_label = html::css_class({
    .margin_bottom = ".25rem",
    .color = "var(--muted)",
    .font_size = ".75rem",
});

static constexpr auto CHAT_JS = R"js(
function chatPendingSend() {
    const inp = document.getElementById('chat-input');
    const t = inp.value.trim();
    if (!t) return;
    const esc = t.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
    revet.opt.appendPending('chat-messages', `<div style="display:flex;justify-content:flex-end;margin:.3rem 0"><div style="max-width:72%;background:var(--accent);color:var(--accent-fg);padding:.5rem .875rem;border-radius:1rem 1rem .25rem 1rem;font-size:.875rem;opacity:.6;line-height:1.4">${esc}<span style="display:block;font-size:.6rem;margin-top:.25rem;opacity:.8">&#x23F3; sending&#x2026;</span></div></div>`);
    inp.value = '';
    inp.focus();
    const f = document.getElementById('chat-messages');
    if (f) f.scrollTop = f.scrollHeight;
}
function chatAfterSwap() {
    revet.opt.removePending('chat-messages');
    const f = document.getElementById('chat-messages');
    if (f) f.scrollTop = f.scrollHeight;
}
document.addEventListener('htmx:sseMessage', function() {
    const f = document.getElementById('chat-messages');
    if (f) setTimeout(() => f.scrollTop = f.scrollHeight, 30);
});
)js";

static auto render_get(const auto& req) {
    static constexpr auto SNIPPET =
"// chat.cpp  —  optimistic message send\n"
"html::form{\n"
"    htmx::attr::post(urls::chat_send),\n"
"    htmx::attr::target(\"#chat-messages\"),\n"
"    htmx::attr::swap_before_end(),\n"
"    htmx::attr::on(\"htmx:before-request\", \"chatPendingSend()\"),\n"
"    htmx::attr::on(\"htmx:after-request\",   \"chatAfterSwap()\"),\n"
"    html::input{html::attr.id(\"chat-input\"), ...},\n"
"    html::button{i18n::chat_send},\n"
"}";

    const auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_chat_eyebrow},
            html::h1{i18n::page_chat_h1},
            html::p{i18n::page_chat_body},
        } +
        html::script{html::raw(CHAT_JS)} +
        html::div{s_window,
            html::div{
                htmx::attr::ext("sse"),
                htmx::sse::connect(urls::chat_stream),
                html::div{
                    k_feed,
                    s_feed,
                    htmx::sse::swap("chat"),
                    htmx::attr::swap_before_end(),
                    html::div{s_alice_row,
                        html::div{s_alice_avatar, "A"},
                        html::div{s_alice_bubble, i18n::chat_alice_intro},
                    },
                },
            },
            html::form{
                htmx::attr::post(urls::chat_send),
                htmx::attr::target(k_feed),
                htmx::attr::swap_before_end(),
                htmx::attr::on("htmx:before-request", "chatPendingSend()"),
                htmx::attr::on("htmx:after-request",  "chatAfterSwap()"),
                s_bar,
                html::input{
                    html::attr.id("chat-input"),
                    html::attr.name("text"),
                    html::attr.type("text"),
                    html::attr.placeholder(i18n::chat_placeholder),
                    html::attr.autocomplete("off"),
                    styles::inp,
                    html::css_class({.flex_grow = "1"}),
                },
                html::button{styles::btn, styles::btn_solid, i18n::chat_send},
            },
        } +
        sec_label("Code") +
        html::div{s_snippet_label, "htmx::optimistic_append — append before request, remove on confirm"} +
        html::pre{SNIPPET};

    return layout_page(req, i18n::page_chat_h1, urls::chat, content);
}
}

namespace route_chat_send {
namespace log = web::log; // ::log is the math function on glibc

static constexpr std::string_view ROW_STYLE =
    "display:flex;justify-content:flex-end;margin:.3rem 0;animation:fadeIn .2s";
static constexpr std::string_view BUBBLE_STYLE =
    "max-width:72%;background:var(--accent);color:var(--accent-fg);"
    "padding:.5rem .875rem;border-radius:1rem 1rem .25rem 1rem;"
    "font-size:.875rem;line-height:1.4";
static constexpr std::string_view STAMP_STYLE =
    "display:block;font-size:.6rem;margin-top:.25rem;opacity:.7";

static auto render_post(const auto& req) -> web::Response {
    const auto text = std::string(req.form.get("text"));
    if (text.empty()) return web::to_response(html::Node{});

    // Simulate server processing latency so optimistic update is visible
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    return web::to_response(html::div{
        html::attr.style(ROW_STYLE),
        html::div{
            html::attr.style(BUBBLE_STYLE),
            text,
            html::span{html::attr.style(STAMP_STYLE), "✓ sent"},
        },
    });
}
}

namespace route_chat_stream {
namespace log = web::log; // ::log is the math function on glibc

static constexpr std::array FRAGMENTS = {
    "Lorem ipsum dolor sit amet.",
    "Consectetur adipiscing elit, sed do eiusmod.",
    "Ut labore et dolore magna aliqua.",
    "Duis aute irure dolor in reprehenderit.",
    "Excepteur sint occaecat cupidatat non proident.",
    "Sunt in culpa qui officia deserunt mollit.",
    "Voluptatem accusantium doloremque laudantium.",
    "Nemo enim ipsam voluptatem quia voluptas sit.",
    "Quis nostrum exercitationem ullam corporis suscipit.",
    "Ut perspiciatis unde omnis iste natus error sit.",
    "Nam libero tempore cum soluta nobis eligendi.",
    "Temporibus autem quibusdam et aut officiis.",
};

static constexpr std::string_view ROW_STYLE =
    "display:flex;align-items:flex-start;gap:.5rem;margin:.3rem 0;animation:fadeIn .2s";
static constexpr std::string_view AVATAR_STYLE =
    "display:flex;width:1.75rem;height:1.75rem;flex-shrink:0;"
    "justify-content:center;align-items:center;border-radius:50%;"
    "background:var(--muted);color:var(--surface);font-size:.7rem;font-weight:700";
static constexpr std::string_view BUBBLE_STYLE =
    "max-width:72%;padding:.5rem .875rem;border:1px solid var(--border);"
    "border-radius:.25rem 1rem 1rem 1rem;background:var(--surface);"
    "font-size:.875rem;line-height:1.4";

static void render_sse(const web::Request& /*req*/, web::SSEWriter& sse) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> pick(0, FRAGMENTS.size() - 1);
    std::uniform_int_distribution<int>    delay_ms(2000, 4500);

    while (sse.alive()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms(rng)));
        if (!sse.alive()) break;

        auto text = std::string(FRAGMENTS[pick(rng)]);
        auto msg = html::div{
            html::attr.style(ROW_STYLE),
            html::div{html::attr.style(AVATAR_STYLE), "A"},
            html::div{html::attr.style(BUBBLE_STYLE), text},
        }.str;
        sse.send(msg, "chat");
    }
}
}

namespace route_components {
namespace log = web::log; // ::log is the math function on glibc

static auto render_get(const auto& req) {
    auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_components_eyebrow},
            html::h1{i18n::page_components_h1},
            html::p{i18n::page_components_body},
        } +

        sec_label(i18n::section_toggle) +
        html::p{i18n::toggle_desc} +
        ui::toggle("intro", i18n::toggle_what_label, html::p{i18n::toggle_what_body}) +
        ui::toggle("features", i18n::toggle_why_label, html::p{i18n::toggle_why_body}) +

        sec_label(i18n::section_accordion) +
        html::p{i18n::section_accordion_body} +
        ui::accordion({
            {i18n::acc_q1, html::p{i18n::acc_a1}},
            {i18n::acc_q2, html::p{i18n::acc_a2}},
            {i18n::acc_q3, html::p{i18n::acc_a3}},
        }) +

        sec_label(i18n::section_modal) +
        html::p{i18n::modal_desc} +
        ui::modal("demo-modal",
            html::span{i18n::modal_open},
            html::fragment(
                html::h3{i18n::modal_title},
                html::p{i18n::modal_body})) +

        sec_label(i18n::section_dropdown) +
        html::p{i18n::dropdown_desc} +
        ui::dropdown(
            html::Node{i18n::dropdown_options},
            ui::dropdown_item(i18n::dd_item_docs,   "/") +
            ui::dropdown_item(i18n::dd_item_source, "/about") +
            ui::dropdown_item(i18n::nav_contact,    urls::contact)) +

        sec_label(i18n::section_tabs) +
        html::p{i18n::tabs_desc} +
        ui::tabs("demo", {
            {"setup",  i18n::tab_setup,
             html::fragment(
                 html::p{i18n::tab_setup_intro},
                 html::pre{"cmake -B build && cmake --build build\n./build/myapp"})},
            {"routes", i18n::tab_routing,
             html::fragment(
                 html::p{i18n::tab_comp_routing_intro},
                 html::pre{"// site/hello.cpp  ->  GET /hello\nstatic auto render_get(const auto& req) {\n    return html::h1{\"Hello, world!\"};\n}"})},
            {"forms",  i18n::tab_forms,
             html::fragment(
                 html::p{i18n::tab_comp_forms_intro},
                 html::pre{"struct SignupForm {\n    [[revet::field(required, email)]]\n    forms::Text email;\n};"})},
        }) +

        sec_label(i18n::section_tooltip) +
        html::p{i18n::tooltip_desc} +
        html::div{styles::demo_row,
            ui::tooltip(html::span{styles::demo_chip,        i18n::tooltip_hover}, i18n::tooltip_text),
            ui::tooltip(html::span{styles::demo_chip_accent, i18n::tooltip_also},  i18n::tooltip_also_text),
        } +

        sec_label(i18n::section_carousel) +
        html::p{html::code{"scroll-snap-type: x mandatory"}, " ", i18n::section_carousel_body} +
        ui::carousel(html::list(std::array{
            std::pair{i18n::car_routing_title, i18n::car_routing_desc},
            std::pair{i18n::car_html_title,    i18n::car_html_desc},
            std::pair{i18n::car_forms_title,   i18n::car_forms_desc},
            std::pair{i18n::car_js_title,      i18n::car_js_desc},
            std::pair{i18n::car_db_title,      i18n::car_db_desc},
        }, [n = 0](const auto& card) mutable {
            return ui::carousel_item(html::div{styles::car_card,
                html::div{styles::car_num, std::format("{:02}", ++n)},
                html::h3{card.first}, html::p{styles::car_desc, card.second}});
        })) +

        sec_label(i18n::section_scroll_reveal) +
        html::p{html::code{"ui::scroll_pin"}, " ", i18n::section_scroll_body, " ",
            html::code{"view-timeline"}, " ", i18n::section_scroll_body2} +
        html::list(std::array{
            std::pair{i18n::car_routing_title, i18n::pin_routing_desc},
            std::pair{i18n::car_html_title,    i18n::pin_html_desc},
            std::pair{i18n::pin_css_title,     i18n::pin_css_desc},
        }, [n = 0](const auto& pin) mutable {
            return ui::scroll_pin(html::fragment(
                html::div{styles::pin_num, std::format("{:02}", ++n)},
                html::h3{styles::pin_h3, pin.first},
                html::p{styles::pin_desc, pin.second}));
        }) +

        sec_label(i18n::section_sidebar_theme) +
        html::p{i18n::sidebar_theme_desc} +
        html::div{styles::sb_demo_wrap, html::attr.id("sb-theme-wrap"),
            ui::sidebar("demo",
                html::fragment(
                    html::div{styles::sb_demo_hd, i18n::sb_demo_nav},
                    html::div{styles::sb_demo_link,        i18n::nav_home},
                    html::div{styles::sb_demo_link_muted,  i18n::nav_users},
                    html::div{styles::sb_demo_link_active, i18n::nav_components},
                    html::div{styles::sb_demo_row,
                        ui::scoped_theme_toggle("sb-demo", false, "#sb-theme-wrap"),
                        html::span{styles::sb_demo_hint, i18n::demo_theme_label},
                    }),
                html::p{styles::sb_demo_body, i18n::sb_demo_main})
        };

    return layout_page(req, i18n::page_components_h1, urls::components, content);
}
}

namespace route_contact {
namespace log = web::log; // ::log is the math function on glibc

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
}

namespace route_interactive {
namespace log = web::log; // ::log is the math function on glibc

static const auto s_pos_rel       = css::make(css::pos::relative);
static const auto s_auto_resize   = html::css_class({.min_height = "4rem"});
static const auto s_char_area     = html::css_class({.min_height = "5rem", .padding_bottom = "1.75rem"});
static const auto s_char_counter  = html::css_class({
    .position = "absolute", .right = ".75rem",    .bottom = ".5rem",
    .color = "var(--muted)", .font_size = ".75rem", .pointer_events = "none",
});
static const auto s_kbd_out       = html::css_class({
    .min_width = "5rem", .color = "var(--accent)", .font_size = ".875rem",
});
static const auto s_scroll_top    = html::css_class({
    .position = "fixed",
    .right = "1.75rem",     .bottom = "1.75rem",
    .width = "2.75rem",     .height = "2.75rem",
    .border = "none",       .border_radius = "50%",
    .background = "var(--accent)",
    .color = "var(--accent-fg)",
    .font_size = "1.1rem",  .font_weight = "700", .line_height = "2.75rem", .text_align = "center",
    .cursor = "pointer",    .z_index = "50",
}).extra("box-shadow:0 4px 16px rgba(0,0,0,.25)");

static auto render_get(const auto& req) {
    auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_interactive_eyebrow},
            html::h1{i18n::page_interactive_h1},
            html::p{i18n::page_interactive_body},
        } +

        // ── Clipboard ────────────────────────────────────────────────────────
        sec_label(i18n::section_clipboard) +
        html::p{i18n::interactive_clipboard_desc_pre, html::code{"data-"}, i18n::interactive_clipboard_desc_suf} +
        html::div{styles::demo_row,
            html::button{
                ui::clipboard::text("https://revet.dev/docs"),
                styles::btn, styles::btn_ghost,
                html::code{"https://revet.dev/docs"},
            },
            html::button{
                ui::clipboard::text("cmake --build build"),
                styles::btn, styles::btn_ghost,
                html::code{"cmake --build build"},
            },
        } +
        snippet_block(i18n::snippet_usage,
            "html::button{ui::clipboard::text(page_url), styles::btn, styles::btn_ghost, \"Copy Link\"}\n"
            "html::span{ui::clipboard::text(api_key), \"📋\"}") +

        // ── Auto-resize ──────────────────────────────────────────────────────
        sec_label(i18n::section_auto_resize) +
        html::p{i18n::interactive_auto_resize_desc_pre,
                html::code{"ui::js::auto_resize()"}, i18n::interactive_auto_resize_desc_mid,
                html::code{"<textarea>"}, "."} +
        html::textarea{
            ui::js::auto_resize(),
            styles::inp,
            s_auto_resize,
            html::attr.name("body"),
            html::attr.placeholder(i18n::interactive_auto_resize_placeholder),
        } +
        snippet_block(i18n::snippet_usage,
            "html::textarea{ui::js::auto_resize(), html::attr.placeholder(\"…\")}") +

        // ── Char counter ─────────────────────────────────────────────────────
        sec_label(i18n::section_char_counter) +
        html::p{i18n::interactive_char_counter_desc} +
        html::div{s_pos_rel,
            html::textarea{
                ui::js::char_counter("char-count"),
                styles::inp,
                s_char_area,
                html::attr.maxlength("280"),
                html::attr.name("tweet"),
                html::attr.placeholder(i18n::interactive_tweet_placeholder),
            },
            html::span{html::attr.id("char-count"), s_char_counter, "0/280"},
        } +
        snippet_block(i18n::snippet_usage,
            "html::textarea{ui::js::char_counter(\"my-count\"), html::attr.maxlength(\"280\")},\n"
            "html::span{html::attr.id(\"my-count\"), \"0/280\"}") +

        // ── Debounce ─────────────────────────────────────────────────────────
        sec_label(i18n::section_debounce) +
        html::p{i18n::interactive_debounce_desc_pre, html::code{"debounced"},
                i18n::interactive_debounce_desc_mid, html::code{"hx-trigger=\"debounced\""},
                i18n::interactive_debounce_desc_suf} +
        html::div{styles::form_wrap,
            html::input{
                ui::js::debounce(400),
                styles::inp,
                html::attr.type("search"),
                html::attr.name("q"),
                html::attr.placeholder(i18n::interactive_search_placeholder),
                htmx::attr::get(urls::interactive_search),
                htmx::attr::trigger("debounced"),
                htmx::attr::target("#country-results"),
            },
            html::div{html::attr.id("country-results")},
        } +
        snippet_block(i18n::snippet_usage_htmx,
            "html::input{\n"
            "    ui::js::debounce(400),\n"
            "    htmx::attr::get(\"/search\"),\n"
            "    htmx::attr::trigger(\"debounced\"),\n"
            "    htmx::attr::target(\"#results\"),\n"
            "}") +

        // ── Keyboard shortcut ────────────────────────────────────────────────
        sec_label(i18n::section_kbd) +
        html::p{i18n::interactive_kbd_desc} +
        html::div{styles::demo_row,
            html::button{
                ui::js::kbd("cmd+k"),
                styles::btn, styles::btn_solid,
                html::attr.onclick(
                    "const s=document.getElementById('kbd-out');"
                    "s.textContent='⌘K fired';"
                    "clearTimeout(s._t);s._t=setTimeout(()=>s.textContent='',1500)"),
                i18n::interactive_kbd_search, html::kbd{"⌘K"},
            },
            html::button{
                ui::js::kbd("shift+?"),
                styles::btn, styles::btn_ghost,
                html::attr.onclick(
                    "const s=document.getElementById('kbd-out');"
                    "s.textContent='? fired';"
                    "clearTimeout(s._t);s._t=setTimeout(()=>s.textContent='',1500)"),
                i18n::interactive_kbd_help, html::kbd{"?"},
            },
            html::span{html::attr.id("kbd-out"), s_kbd_out},
        } +
        snippet_block(i18n::snippet_usage,
            "html::button{ui::js::kbd(\"cmd+k\"), \"Search \", html::kbd{\"⌘K\"}}\n"
            "html::button{ui::js::kbd(\"escape\"), \"Close\"}") +

        // ── Scroll to top ─────────────────────────────────────────────────────
        sec_label(i18n::section_scroll_to_top) +
        html::p{i18n::interactive_scroll_top_desc_pre,
                html::code{"ui::js::scroll_to_top()"}, i18n::interactive_scroll_top_desc_suf} +
        snippet_block(i18n::snippet_usage,
            "html::button{\n"
            "    ui::js::scroll_to_top(),\n"
            "    \"↑\",\n"
            "}") +

        // ── More utilities ───────────────────────────────────────────────────
        sec_label(i18n::section_other_js) +
        html::p{i18n::interactive_more_utilities_pre, html::code{"ui::js::"}, ":"} +
        card_grid(html::fragment(
            card("click_outside()", i18n::js_click_outside_title, i18n::js_click_outside_body),
            card("focus_trap()",    i18n::js_focus_trap_title,    i18n::js_focus_trap_body),
            card("geolocate(sel)",  i18n::js_geolocate_title,     i18n::js_geolocate_body),
            card("fullscreen(sel)", i18n::js_fullscreen_title,    i18n::js_fullscreen_body),
            card("paste_zone()",    i18n::js_paste_zone_title,    i18n::js_paste_zone_body),
            card("share()",         i18n::js_share_title,         i18n::js_share_body)
        )) +

        html::button{ui::js::scroll_to_top(), s_scroll_top, "↑"};

    return layout_page(req, i18n::page_interactive_h1, urls::interactive, content);
}
}

namespace route_interactive_search {
namespace log = web::log; // ::log is the math function on glibc

static const auto s_result_row   = html::css_class({.padding = ".45rem .875rem", .font_size = ".875rem"})
                                        .extra("border-bottom:1px solid var(--border)");
static const auto s_no_match     = html::css_class({.padding = ".5rem 0", .color = "var(--muted)", .font_size = ".875rem"});
static const auto s_results_wrap = html::css_class({
    .margin_top = ".5rem",
    .overflow = "hidden",
    .border = "1px solid var(--border)", .border_radius = "var(--r)",
    .background = "var(--surface)",
});

static constexpr std::array COUNTRIES = {
    "Afghanistan", "Albania", "Algeria", "Argentina", "Australia",
    "Austria", "Bangladesh", "Belgium", "Bolivia", "Brazil", "Cambodia",
    "Canada", "Chile", "China", "Colombia", "Croatia", "Czech Republic",
    "Denmark", "Ecuador", "Egypt", "Estonia", "Ethiopia", "Finland",
    "France", "Germany", "Ghana", "Greece", "Hungary", "India",
    "Indonesia", "Iran", "Iraq", "Ireland", "Israel", "Italy",
    "Japan", "Jordan", "Kenya", "Latvia", "Lithuania", "Malaysia",
    "Mexico", "Morocco", "Netherlands", "New Zealand", "Nigeria",
    "Norway", "Pakistan", "Peru", "Philippines", "Poland", "Portugal",
    "Romania", "Russia", "Saudi Arabia", "Singapore", "Slovakia",
    "Slovenia", "South Africa", "South Korea", "Spain", "Sweden",
    "Switzerland", "Thailand", "Turkey", "Ukraine", "United Arab Emirates",
    "United Kingdom", "United States", "Uruguay", "Venezuela", "Vietnam",
};

static auto render_get(const auto& req) -> web::Response {
    const auto q = std::string(req.query.get("q"));
    if (q.empty())
        return web::to_response(html::Node{});

    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };
    const auto ql = lower(q);

    html::Node rows;
    int count = 0;
    for (auto name : COUNTRIES) {
        if (lower(std::string(name)).find(ql) != std::string::npos) {
            rows = std::move(rows) + html::div{s_result_row, name};
            if (++count == 8) break;
        }
    }

    if (!count)
        return web::to_response(html::p{s_no_match, "No matches."});

    return web::to_response(html::div{s_results_wrap, std::move(rows)});
}
}

namespace route_items {
namespace log = web::log; // ::log is the math function on glibc
static auto render_get(const web::Request& req) -> web::Task<web::Response> { co_return web::Response{}; }
}

namespace route_live {
namespace log = web::log; // ::log is the math function on glibc

static const auto color_grid_style = css::flex(css::wrap::wrap, css::gap::_3)
    .extra("padding:1.5rem 0;min-height:160px");

static auto render_get(const auto& req) {
    auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_live_eyebrow},
            html::h1{i18n::page_live_h1},
            html::p{i18n::page_live_body},
        } +
        html::style{"@keyframes sw-in{from{opacity:0;transform:scale(.7)}to{opacity:1;transform:scale(1)}}"} +
        html::div{
            htmx::attr::ext("sse"),
            htmx::sse::connect("/live/stream"),
            html::div{
                html::attr.id("color-grid"),
                htmx::sse::swap("color"),
                htmx::attr::swap_before_end(),
                color_grid_style,
            },
        };

    return layout_page(req, i18n::page_live_h1, urls::live, content);
}
}

namespace route_live_stream {
namespace log = web::log; // ::log is the math function on glibc

static constexpr std::string_view SWATCH_WRAP =
    "animation:sw-in .3s;display:flex;flex-direction:column;align-items:center;gap:6px";
static constexpr std::string_view SWATCH_CHIP =
    ";width:72px;height:72px;border-radius:8px;border:1px solid rgba(128,128,128,.15)";
static constexpr std::string_view SWATCH_LABEL =
    "font-size:.6rem;color:var(--muted);font-family:monospace";

static void render_sse(const web::Request& /*req*/, web::SSEWriter& sse) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<unsigned int> dist(0, 0xFFFFFFU);

    while (sse.alive()) {
        unsigned int val = dist(rng);
        auto hex = std::format("#{:06X}", val);

        auto swatch = html::div{
            html::attr.style(SWATCH_WRAP),
            html::div{html::attr.style(std::format("background:{}{}", hex, SWATCH_CHIP))},
            html::code{html::attr.style(SWATCH_LABEL), hex},
        }.str;
        sse.send(swatch, "color");
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }
}
}

namespace route_quakes {
namespace log = web::log; // ::log is the math function on glibc

static const log::TuTag LOG(log::tags::ROUTES);

// GET /quakes?start=YYYY-MM-DD&end=YYYY-MM-DD
// Outbound HTTP demo against the USGS Earthquake API.
// usgs_gov::count() is generated from demo/app/apis/usgs.hpp.
static auto render_get(const web::Request& req) -> web::Task<web::Response> {
    auto start = req.query.get("start");
    auto end   = req.query.get("end");
    if (start.empty()) start = "2024-01-01";
    if (end.empty())   end   = "2024-01-02";

    const auto data = co_await usgs_gov::count(start, end);

    const auto content = html::div{
        html::h1{"USGS Earthquakes"},
        html::p{html::strong{"Window: "}, start, " → ", end},
        html::p{html::strong{"Count: "}, html::span{html::id{"quake-count"}, data.count}},
        html::p{html::strong{"Max allowed: "}, data.maxAllowed}
    };

    co_return layout_page(req, "Quakes", "/quakes", content);
}
}

namespace route_set_locale {
namespace log = web::log; // ::log is the math function on glibc
// GET /set_locale?lang=es  — sets the locale cookie and redirects back.

static auto render_get(const web::Request& req) -> web::Response {
    auto lang = req.query.get("lang");
    // Validate against known locales; fall back to "en".
    bool valid = false;
    for (int i = 0; i < i18n::locale_count; ++i)
        if (i18n::locale_names[i] == lang) { valid = true; break; }
    if (!valid) lang = "en";

    auto ref = req.headers.get("referer", "/");
    auto resp = web::redirect(ref);
    resp.cookies.data["locale"] = std::string(lang);
    return resp;
}
}

namespace route_site_init {
namespace log = web::log; // ::log is the math function on glibc

static const log::TuTag LOG(log::tags::APP);

void site_init() {
    web::set_public_root(paths::public_dir);
    // CSRF for HTML routes; API routes (/api/*) use Bearer token auth instead.
    web::use([](const web::Request& req, const web::Next& next) -> web::Response {
        if (std::string_view(req.path.path).starts_with("/api/"))
            return next();
        return web::csrf::middleware(req, next);
    });

    // Set thread-local locale from cookie before every request.
    web::use([](const web::Request& req, const web::Next& next) {
        const auto lang = req.cookies.get("locale");
        i18n::set_locale(lang.empty() ? "en" : lang);
        return next();
    });

    web::on_error(web::status::not_found, [](const web::Request&) {
        return web::to_response(
            html::h1{i18n::page_not_found_h1} +
            html::p{i18n::page_not_found_body}
        );
    });

    log::info("Site initialized with CSRF protection and i18n middleware.");
}
}

namespace route_upload {
namespace log = web::log; // ::log is the math function on glibc

static auto route_guards() -> std::vector<web::Middleware> {
    return {
        uploads::allow({
            .validated_dir = std::filesystem::path(uploads::files::validated_dir),
            .types         = { web::FileType::jpeg, web::FileType::png,
                               web::FileType::gif,  web::FileType::webp },
        })
    };
}

static constexpr auto type_label(web::FileType t) -> std::string_view {
    switch (t) {
        case web::FileType::jpeg:    return "JPEG";
        case web::FileType::png:     return "PNG";
        case web::FileType::gif:     return "GIF";
        case web::FileType::webp:    return "WebP";
        case web::FileType::pdf:     return "PDF";
        case web::FileType::zip:     return "ZIP";
        case web::FileType::unknown: return "unknown";
    }
    return "unknown";
}

static auto fmt_size(std::uintmax_t bytes) -> std::string {
    if (bytes < 1024UL)         return std::format("{} B", bytes);
    if (bytes < 1024UL * 1024UL) return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

// ── Snippets ──────────────────────────────────────────────────────────────────

static constexpr auto SNIPPET_TOML =
R"([uploads.files]
# incoming  = "app/uploads/files/incoming"   # auto-created, mode 0700
# validated = "app/uploads/files/validated"  # auto-created, mode 0750)";

static constexpr auto SNIPPET_ROUTE =
R"(#include <revet/upload.hpp>

// Validates uploads before render_get / render_post runs.
// On failure: deletes tmp files, returns 422. Never calls your handler.
static auto route_guards() -> std::vector<web::Middleware> {
    return {
        uploads::allow({
            .validated_dir = std::filesystem::path(uploads::files::validated_dir),
            .types         = { web::FileType::jpeg, web::FileType::png,
                               web::FileType::gif,  web::FileType::webp },
        })
    };
}

static auto render_post(const web::Request&) -> web::Response {
    auto& files = web::Context::current().files;
    if (files.empty()) return web::redirect(urls::upload);

    // files[0].path          — absolute path in validated_dir
    // files[0].filename      — sanitized original filename
    // files[0].detected_type — detected from magic bytes (not Content-Type header)
    std::error_code ec;
    auto bytes = std::filesystem::file_size(files[0].path, ec);
    // … process the file …
    return web::redirect(urls::upload);
})";

static constexpr auto SNIPPET_SIZE_GUARD =
R"(uploads::allow({
    .validated_dir = std::filesystem::path(uploads::files::validated_dir),
    .types         = { web::FileType::jpeg, web::FileType::png },
    .validators    = { uploads::max_size(5 * 1024 * 1024) },  // 5 MB cap
}))";

static constexpr auto SNIPPET_CUSTOM_VALIDATOR =
R"(// Any std::function<bool(web::UploadedFile&)> works as a validator.
// Receives a mutable file ref — inspect on disk, add metadata, etc.
auto square_only = [](web::UploadedFile& f) -> bool {
    // e.g. open f.path and check image dimensions
    return true;
};

uploads::allow({
    .validated_dir = std::filesystem::path(uploads::files::validated_dir),
    .validators    = { uploads::magic_bytes({web::FileType::png}), square_only },
    .check_magic   = false,  // magic_bytes validator handles type detection
}))";

// ── Render ────────────────────────────────────────────────────────────────────

static auto render_get(const web::Request& req) -> web::Response {
    const auto upload_form =
        html::div{styles::form_wrap,
            html::form{
                html::attr.action(urls::upload),
                html::attr.method("POST"),
                html::attr.enctype("multipart/form-data"),
                web::csrf::field(req),
                html::div{styles::fgroup,
                    html::label{html::attr.for_("file"), i18n::upload_file_label},
                    html::file_input{
                        html::attr.id("file"),
                        html::attr.name("file"),
                        html::attr.accept_images(),
                    },
                },
                html::submit{styles::btn, i18n::btn_upload},
            }
        };

    const auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_upload_eyebrow},
            html::h1{i18n::page_upload_h1},
            html::p{i18n::page_upload_body},
        } +

        sec_label(i18n::section_upload_form) +
        html::p{i18n::upload_form_desc_a, html::code{"enctype=\"multipart/form-data\""},
                i18n::upload_form_desc_b, html::code{"route_guards()"},
                i18n::upload_form_desc_c, html::code{"validated_dir"},
                i18n::upload_form_desc_d} +
        upload_form +

        sec_label("revet.toml") +
        html::p{i18n::upload_toml_desc} +
        snippet_block("revet.toml", SNIPPET_TOML) +

        sec_label(i18n::section_route_handler) +
        html::p{html::code{"uploads::allow"}, i18n::upload_handler_desc_a,
                html::code{"Content-Type"}, i18n::upload_handler_desc_b} +
        snippet_block("site/upload.cpp", SNIPPET_ROUTE) +

        sec_label(i18n::section_size_guard) +
        html::p{html::code{"uploads::max_size(bytes)"}, i18n::upload_size_guard_desc,
                html::code{".validators"}, "."} +
        snippet_block("5 MB cap", SNIPPET_SIZE_GUARD) +

        sec_label(i18n::section_custom_validators) +
        html::p{i18n::upload_custom_val_desc_pre,
                html::code{"std::function<bool(web::UploadedFile&)>"},
                i18n::upload_custom_val_desc_suf} +
        snippet_block("Custom validator", SNIPPET_CUSTOM_VALIDATOR);

    return layout_page(req, i18n::page_upload_h1, urls::upload, content);
}

static auto render_post(const web::Request& req) -> web::Response {
    auto& files = web::Context::current().files;
    if (files.empty()) return web::redirect(urls::upload);

    const auto& f = files[0];
    std::error_code ec;
    const auto bytes    = std::filesystem::file_size(f.path, ec);
    const auto size_str = ec ? std::string{"(unknown)"} : fmt_size(bytes);

    const auto result =
        alert_ok(
            html::strong{i18n::upload_file_accepted} +
            html::div{styles::alert_detail,
                html::div{html::strong{i18n::upload_label_name},          f.filename},
                html::div{html::strong{i18n::upload_label_detected_type}, type_label(f.detected_type)},
                html::div{html::strong{i18n::upload_label_declared_type}, f.content_type},
                html::div{html::strong{i18n::upload_label_size},          size_str},
                html::div{html::strong{i18n::upload_label_stored_at},     f.path.string()},
            }
        );

    const auto content =
        html::div{styles::hero,
            html::h1{i18n::upload_accepted_h1},
        } +
        sec_label(i18n::section_upload_result) +
        result +
        html::div{css::make(css::mt::_6),
            html::a{html::attr.href(urls::upload), styles::btn, styles::btn_ghost, i18n::btn_upload_another},
        };

    return layout_page(req, i18n::page_upload_h1, urls::upload, content);
}
}

namespace route_users {
namespace log = web::log; // ::log is the math function on glibc

static auto render_get(const auto& req) {
    const auto rows = html::list(USERS, [](const auto& entry) {
        const auto& [id, info] = entry;
        const auto& [name, email] = info;
        return html::tr{
            html::td{html::div{styles::u_name,
                html::div{styles::avatar, name.substr(0, 1)},
                html::div{
                    html::a{html::attr.href(urls::users__id_(id)), name},
                    ui::tooltip(
                        html::span{styles::u_tag, i18n::user_staff_badge},
                        i18n::user_staff_tooltip
                    )
                }
            }},
            html::td{email},
            html::td{html::code{id}},
        };
    });

    const auto content =
        html::div{styles::hero,
            html::div{styles::eyebrow, i18n::page_users_eyebrow},
            html::h1{i18n::page_users_h1},
            html::p{i18n::page_users_body},
        } +
        sec_label(i18n::section_staff_directory) +
        html::table{styles::u_table,
            html::thead{html::tr{
                html::th{i18n::col_name}, html::th{i18n::col_email}, html::th{i18n::col_id},
            }},
            html::tbody{std::move(rows)},
        };

    return layout_page(req, i18n::page_users_h1, urls::users, content);
}
}

namespace route_ws_echo {
namespace log = web::log; // ::log is the math function on glibc
static void render_ws(const web::Request& req, web::State<Cart> cart, web::ws::Conn& conn) {
    (void)req; (void)cart; (void)conn;
}
static auto route_guards() -> std::vector<web::Middleware> { return {}; }
}

namespace route_fonts__file_ {
namespace log = web::log; // ::log is the math function on glibc

static auto render_get(const web::Request& req) -> web::Response {
    auto file = std::string(req.params.get("file"));
    if (file.find("..") != std::string::npos || !file.ends_with(".woff2"))
        return {.body = "Not Found", .status = web::status::not_found};
    return web::serve_file("fonts/" + file);
}
}

namespace route_items__id__edit_form {
namespace log = web::log; // ::log is the math function on glibc
static auto render_get(const web::Request& req, web::State<Cart> cart,
                       web::Flash<Notice> notice) -> web::Response {
    return {};
}
web::Task<web::Response> render_post(const web::Request& req, web::Flash<Notice> notice) {
    auto ctx2 = web::Context::current();
    ctx2.flash(Notice{"saved", false});
    ctx2.has<Cart>();
    co_return web::Response{};
}
static void render_sse(const web::Request& req, web::Flash<Notice> n, web::SSEWriter& sse) {}
}

namespace route_users__id_ {
namespace log = web::log; // ::log is the math function on glibc

static auto render_get(const auto& req) {
    const auto id = req.params.get("id");

    if (!USERS.contains(id)) {
        const auto content =
            breadcrumb(urls::users, i18n::nav_users) +
            html::div{styles::hero,
                html::h1{i18n::page_user_not_found},
                html::p{i18n::user_not_found_body},
            } +
            html::a{html::attr.href(urls::users), styles::btn, styles::btn_ghost, i18n::btn_back_to_list};

        auto r = layout_page(req, i18n::page_user_not_found, urls::users, content);
        r.status = web::status::not_found;
        return r;
    }

    const auto& [name, email] = USERS.at(id);

    const auto content =
        breadcrumb(urls::users, i18n::nav_users) +
        html::div{styles::profile,
            html::div{styles::avatar_lg, name.substr(0, 1)},
            html::div{
                html::div{styles::profile_name, name},
                html::div{styles::profile_meta,
                    i18n::label_email, " ", html::code{email}, html::br{},
                    i18n::label_id,    " ", html::span{styles::chip, id}
                }
            }
        } +
        sec_label(i18n::section_account_activity) +
        html::div{styles::cards,
            card(i18n::card_last_login, i18n::card_last_login_val, i18n::card_last_login_detail),
            card(i18n::card_status,     i18n::card_status_val,     i18n::card_status_detail),
        };

    return layout_page(req, name, urls::users, content);
}
}

namespace routes {
void setup(std::vector<web::Route>& out) {
    {
        web::Route r; r.path = "/_revet/htmx.js";
        r.get = [](const web::Request&) -> web::Response {
            web::Response resp;
            resp.body = std::string(revet::detail::htmx_js);
            resp.content_type = "application/javascript; charset=utf-8";
            resp.headers["Cache-Control"] = "public, max-age=86400";
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/_revet/optimistic.js";
        r.get = [](const web::Request&) -> web::Response {
            web::Response resp;
            resp.body = std::string(revet::detail::optimistic_js);
            resp.content_type = "application/javascript; charset=utf-8";
            resp.headers["Cache-Control"] = "public, max-age=86400";
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/_app/9-lives.min.f2acf862b5.js";
        r.get = [](const web::Request&) -> web::Response {
            web::Response resp;
            resp.body = std::string(scripts::_9_lives_min_js);
            resp.content_type = "application/javascript; charset=utf-8";
            resp.headers["Cache-Control"] = "public, max-age=31536000, immutable";
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/_app/chart.85c991bf8f.js";
        r.get = [](const web::Request&) -> web::Response {
            web::Response resp;
            resp.body = std::string(scripts::chart_js);
            resp.content_type = "application/javascript; charset=utf-8";
            resp.headers["Cache-Control"] = "public, max-age=31536000, immutable";
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/toggle_compact";
        r.post = [](const web::Request& req) -> web::Response {
            bool cur = web::toggles::compact(req.cookies);
            web::Response resp;
            if (req.headers.is("hx-request")) {
                resp.status = web::status::no_content;
            } else {
                resp = web::back(req);
            }
            resp.cookies.data["_cw_compact"] = cur ? "0" : "1";
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/toggle_sidebar";
        r.post = [](const web::Request& req) -> web::Response {
            bool cur = web::toggles::sb_open(req.cookies);
            web::Response resp;
            if (req.headers.is("hx-request")) {
                resp.status = web::status::no_content;
            } else {
                resp = web::back(req);
            }
            resp.cookies.data["_cw_sb_open"] = cur ? "0" : "1";
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/toggle_theme";
        r.post = [](const web::Request& req) -> web::Response {
            bool cur = web::toggles::dark_mode(req.cookies);
            web::Response resp;
            if (req.headers.is("hx-request")) {
                resp.status = web::status::no_content;
            } else {
                resp = web::back(req);
            }
            resp.cookies.data["_cw_dark_mode"] = cur ? "0" : "1";
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_index::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/about";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_about::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/about/team";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_about_team::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/api/v1/users";
        r.get = [](const web::Request& req) -> web::Response {
            return web::to_response(route_api_v1_users::render_get(req));
        };
        r.post = [](const web::Request& req) -> web::Response {
            if (!std::string_view(req.headers.get("content-type")).starts_with("application/json"))
                return web::api::error(web::status::bad_request, "Content-Type: application/json required");
            if (req.raw_body.empty())
                return web::api::error(web::status::bad_request, "request body required");
            CreateUser body{};
            try {
                revet::json::Document doc(req.raw_body);
                body = revet::json::from_json<CreateUser>(doc);
            } catch (const revet::json::ParseError& e) {
                return web::api::error(web::status::bad_request, e.what());
            }
            auto result = route_api_v1_users::render_post(body);
            return web::Response{.body = revet::json::to_json<User>(result), .content_type = "application/json"};
        };
        r.guards = route_api_v1_users::api_guards();
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/api/v2/items";
        r.async_get = [](const web::Request& req) -> web::Task<web::Response> {
            auto result = co_await route_api_v2_items::render_get(req);
            co_return web::Response{.body = revet::json::to_json<std::vector<Item>>(result), .content_type = "application/json"};
        };
        r.async_put = [](const web::Request& req) -> web::Task<web::Response> {
            if (!std::string_view(req.headers.get("content-type")).starts_with("application/json"))
                co_return web::api::error(web::status::bad_request, "Content-Type: application/json required");
            if (req.raw_body.empty())
                co_return web::api::error(web::status::bad_request, "request body required");
            Item body{};
            try {
                revet::json::Document doc(req.raw_body);
                body = revet::json::from_json<Item>(doc);
            } catch (const revet::json::ParseError& e) {
                co_return web::api::error(web::status::bad_request, e.what());
            }
            auto result = co_await route_api_v2_items::render_put(body);
            co_return web::Response{.body = revet::json::to_json<Item>(result), .content_type = "application/json"};
        };
        r.del = [](const web::Request& req) -> web::Response {
            return web::to_response(route_api_v2_items::render_delete(req));
        };
        r.patch = [](const web::Request& req) -> web::Response {
            if (!std::string_view(req.headers.get("content-type")).starts_with("application/json"))
                return web::api::error(web::status::bad_request, "Content-Type: application/json required");
            if (req.raw_body.empty())
                return web::api::error(web::status::bad_request, "request body required");
            Item body{};
            try {
                revet::json::Document doc(req.raw_body);
                body = revet::json::from_json<Item>(doc);
            } catch (const revet::json::ParseError& e) {
                return web::api::error(web::status::bad_request, e.what());
            }
            return web::to_response(route_api_v2_items::render_patch(body));
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/chat";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_chat::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/chat/send";
        r.post = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_chat_send::render_post(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/chat/stream";
        r.sse = [](const web::Request& req, web::SSEWriter& sse) {
            route_chat_stream::render_sse(req, sse);
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/components";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_components::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/contact";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_contact::render_get(req));
            return resp;
        };
        r.post = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_contact::render_post(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/interactive";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_interactive::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/interactive/search";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_interactive_search::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/items";
        r.async_get = [](const web::Request& req) -> web::Task<web::Response> {
            return route_items::render_get(req);
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/live";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_live::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/live/stream";
        r.sse = [](const web::Request& req, web::SSEWriter& sse) {
            route_live_stream::render_sse(req, sse);
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/quakes";
        r.async_get = [](const web::Request& req) -> web::Task<web::Response> {
            return route_quakes::render_get(req);
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/set_locale";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_set_locale::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/site_init";
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/upload";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_upload::render_get(req));
            return resp;
        };
        r.post = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_upload::render_post(req));
            return resp;
        };
        r.guards = route_upload::route_guards();
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/users";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_users::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/ws/echo";
        r.ws = [](const web::Request& req, web::ws::Conn& conn) {
            auto& ctx = web::Context::current();
            web::State<Cart> state_Cart;
            if (auto c = req.cookies.get("_cw_Cart"); !c.empty())
                state_Cart.data = web::states::unpack_Cart(c);
            ctx.states[typeid(Cart)] = state_Cart;
            route_ws_echo::render_ws(req, state_Cart, conn);
        };
        r.guards = route_ws_echo::route_guards();
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/fonts/:file";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_fonts__file_::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/items/:id/edit-form";
        r.get = [](const web::Request& req) -> web::Response {
            auto& ctx = web::Context::current();
            web::Flash<Notice> state_Notice;
            if (auto c = req.cookies.get("_cw_Notice"); !c.empty())
                state_Notice.data = web::states::unpack_Notice(c);
            ctx.states[typeid(Notice)] = state_Notice;
            web::State<Cart> state_Cart;
            if (auto c = req.cookies.get("_cw_Cart"); !c.empty())
                state_Cart.data = web::states::unpack_Cart(c);
            ctx.states[typeid(Cart)] = state_Cart;
            auto resp = web::to_response(route_items__id__edit_form::render_get(req, state_Notice, state_Cart));
            if (req.cookies.get("_cw_Notice") != "") resp.cookies["_cw_Notice"] = "";
            return resp;
        };
        r.async_post = [](const web::Request& req) -> web::Task<web::Response> {
            auto& ctx = web::Context::current();
            web::Flash<Notice> state_Notice;
            if (auto c = req.cookies.get("_cw_Notice"); !c.empty())
                state_Notice.data = web::states::unpack_Notice(c);
            ctx.states[typeid(Notice)] = state_Notice;
            return route_items__id__edit_form::render_post(req, state_Notice);
        };
        r.sse = [](const web::Request& req, web::SSEWriter& sse) {
            auto& ctx = web::Context::current();
            web::Flash<Notice> state_Notice;
            if (auto c = req.cookies.get("_cw_Notice"); !c.empty())
                state_Notice.data = web::states::unpack_Notice(c);
            ctx.states[typeid(Notice)] = state_Notice;
            route_items__id__edit_form::render_sse(req, state_Notice, sse);
        };
        out.push_back(r);
    }
    {
        web::Route r; r.path = "/users/:id";
        r.get = [](const web::Request& req) -> web::Response {
            auto resp = web::to_response(route_users__id_::render_get(req));
            return resp;
        };
        out.push_back(r);
    }
}
} // namespace routes