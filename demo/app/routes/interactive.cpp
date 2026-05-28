#include "routes.hpp"
#include "styles.hpp"
#include <revet/htmx.hpp>
#include <revet/ui.hpp>

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
