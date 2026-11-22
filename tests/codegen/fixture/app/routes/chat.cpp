#include "routes.hpp"
#include "styles.hpp"
#include <revet/htmx.hpp>

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
