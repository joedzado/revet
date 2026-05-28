#pragma once
#include "css.hpp"

#include <atomic>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>

// ── ui:: CSS-only interactive components ──────────────────────────────────────
//
// All static CSS lives in the project stylesheet (fixed class names prefixed
// ui-<component>-<role>). Only two things are generated at render time:
//   1. Per-instance view-timeline bindings (scroll_pin)
//   2. Per-instance body:has(#id:checked) blocks (theme_toggle CSS vars, tabs panels)
//
// detail::cls() emits a fixed class name into the element tree without
// injecting rules — the stylesheet already has them.

namespace ui {

namespace detail {
inline auto cls(std::string_view name) -> html::Node {
    return html::Node{html::raw_tag{}, std::format(" .{}", name)};
}
} // namespace detail

// ── CSS helpers ───────────────────────────────────────────────────────────────

inline auto smooth_scroll() -> html::Node {
    html::detail::ui_css_registry.emplace("smooth_scroll", "html{scroll-behavior:smooth}");
    return {};
}

inline auto reduced_motion() -> html::Node {
    html::detail::ui_css_registry.emplace("reduced_motion",
                                          "@media(prefers-reduced-motion:reduce)"
                                          "{*,*::before,*::after{animation-duration:.01ms!important;"
                                          "transition-duration:.01ms!important;scroll-behavior:auto!important}}");
    return {};
}

inline auto auto_dark() -> html::Node {
    html::detail::ui_css_registry.emplace("auto_dark", "@media(prefers-color-scheme:dark)"
                                                       "{.ui-modal,.ui-dropdown-menu{color-scheme:dark}}");
    return {};
}

// scroll_animations() is a no-op — CSS lives in the project stylesheet.
inline auto scroll_animations() -> html::Node {
    return {};
}

// Apply to any element: CSS lives in the project stylesheet (.ui-scroll-anim).
inline const html::Node scroll_anim{html::raw_tag{}, " .ui-scroll-anim"};

// ── toggle ────────────────────────────────────────────────────────────────────
// ui::toggle("id", "Label", content)

inline auto toggle(std::string_view id, std::string_view label, html::Node content) -> html::Node {
    const auto fid = std::format("uitgl-{}", id);
    return html::div{
        detail::cls("ui-tgl-wrap"),
        html::input{html::attr.type("checkbox"), detail::cls("ui-tgl-cb"), html::attr.id(fid)},
        html::label{
            html::attr.for_(fid),
            detail::cls("ui-tgl-btn"),
            label,
            html::span{detail::cls("ui-tgl-arrow"), html::raw("&#x203A;")},
        },
        html::div{detail::cls("ui-tgl-body"), std::move(content)},
    };
}

// ── accordion ─────────────────────────────────────────────────────────────────
// ui::accordion({{"Title", html::p{"Body"}}, ...})

struct accordion_item {
    std::string_view title;
    html::Node body;
};

inline auto accordion(std::initializer_list<accordion_item> items) -> html::Node {
    html::Node children;
    for (const auto& item : items) {
        children = std::move(children) + html::details{
                                             html::summary{item.title},
                                             html::div{detail::cls("ui-acc-body"), item.body},
                                         };
    }
    return html::div{detail::cls("ui-acc-wrap"), std::move(children)};
}

// ── modal ─────────────────────────────────────────────────────────────────────
// ui::modal("id", trigger_node, body_node)

inline auto modal(std::string_view id, html::Node trigger, html::Node body) -> html::Node {
    const auto sid = std::string(id);
    return html::raw("") +
           html::button{
               html::attr("popovertarget", sid),
               detail::cls("ui-modal-btn"),
               std::move(trigger),
           } +
           html::div{
               html::attr.id(sid),
               html::raw(" popover"),
               detail::cls("ui-modal"),
               std::move(body),
               html::div{
                   detail::cls("ui-modal-close"),
                   html::button{
                       html::attr("popovertarget", sid),
                       html::attr("popovertargetaction", "hide"),
                       detail::cls("ui-modal-btn"),
                       "Close",
                   },
               },
           };
}

// ── dropdown ──────────────────────────────────────────────────────────────────
// ui::dropdown(trigger_node, items_node)
// Open/close via container :has() — no instance CSS emitted.
// Counter still generates unique id/for pairs for label-input click binding.

inline auto dropdown_item(std::string_view label, std::string_view href = "") -> html::Node {
    if (!href.empty()) {
        return html::a{html::attr.href(href), detail::cls("ui-dd-item"), label};
    }
    return html::div{detail::cls("ui-dd-item"), html::attr("tabindex", "0"), label};
}

inline auto dropdown(html::Node trigger, html::Node items, std::string_view id = "dd") -> html::Node {
    static std::atomic<int> counter{0};
    const auto fid = std::format("{}-{}", id, counter++);
    return html::div{
        detail::cls("ui-dd-wrap"),
        html::input{detail::cls("ui-dd-cb"), html::attr.type("checkbox"), html::attr.id(fid)},
        html::label{detail::cls("ui-dd-trigger"), html::attr.for_(fid), std::move(trigger)},
        html::label{detail::cls("ui-dd-backdrop"), html::attr.for_(fid)},
        html::div{detail::cls("ui-dd-menu"), std::move(items)},
    };
}

// ── tabs ──────────────────────────────────────────────────────────────────────
// ui::tabs("group-id", {{"tab-id", "Label", content}, ...})
// Static classes in stylesheet; one display:block rule per panel stays inline
// because it maps a stable caller-provided radio ID to a panel ID.

struct tab_item {
    std::string_view id;
    std::string_view label;
    html::Node content;
};

inline auto tabs(std::string_view base_id, std::initializer_list<tab_item> items) -> html::Node {
    std::string inst_css;
    html::Node nav_nodes, panel_nodes;
    bool first = true;
    for (const auto& item : items) {
        const auto fid = std::format("{}-{}", base_id, item.id);
        const auto panel_id = std::format("uitabp-{}", fid);
        inst_css += "body:has(#" + fid + ":checked) #" + panel_id + "{display:block}";
        nav_nodes = std::move(nav_nodes) +
                    html::input{
                        html::attr.type("radio"),    html::attr.name(std::string(base_id)),         html::attr.id(fid),
                        detail::cls("ui-tab-radio"), (first ? html::attr.checked() : html::Node{}),
                    } +
                    html::label{html::attr.for_(fid), detail::cls("ui-tab-label"), item.label};
        panel_nodes =
            std::move(panel_nodes) + html::div{html::attr.id(panel_id), detail::cls("ui-tab-panel"), item.content};
        first = false;
    }
    return html::style{html::raw(inst_css)} + html::div{
                                                  detail::cls("ui-tab-wrap"),
                                                  html::div{detail::cls("ui-tab-nav"), std::move(nav_nodes)},
                                                  std::move(panel_nodes),
                                              };
}

// ── tooltip ───────────────────────────────────────────────────────────────────
// ui::tooltip(trigger_node, "Tooltip text")

inline auto tooltip(html::Node trigger, std::string_view tip) -> html::Node {
    return html::span{
        detail::cls("ui-tip-wrap"),
        std::move(trigger),
        html::span{detail::cls("ui-tip-text"), tip},
    };
}

// ── carousel ──────────────────────────────────────────────────────────────────
// ui::carousel({node1, node2, node3})

inline auto carousel_item(html::Node content) -> html::Node {
    return html::div{detail::cls("ui-car-item"), std::move(content)};
}
inline auto carousel(std::initializer_list<html::Node> items) -> html::Node {
    html::Node children;
    for (const auto& item : items) {
        children = std::move(children) + carousel_item(item);
    }
    return html::div{detail::cls("ui-car-wrap"), std::move(children)};
}
inline auto carousel(html::Node items) -> html::Node {
    return html::div{detail::cls("ui-car-wrap"), std::move(items)};
}

// ── scroll_pin ────────────────────────────────────────────────────────────────
// Sticky-scroll card. Static classes in stylesheet; only the per-instance
// view-timeline name is generated here.

inline auto scroll_pin(html::Node content) -> html::Node {
    static std::atomic<int> sp_counter{0};
    const auto n = sp_counter++;
    const auto outer_id = std::format("sp-o-{}", n);
    const auto card_id = std::format("sp-c-{}", n);
    const auto tl_name = std::format("--sp{}", n);
    std::string inst_css = "#" + outer_id + "{view-timeline:" + tl_name +
                           " block}"
                           "#" +
                           card_id +
                           "{"
                           "animation:_ui_pin linear both;"
                           "animation-timeline:" +
                           tl_name +
                           ";"
                           "animation-range:entry 0% exit 100%"
                           "}";
    return html::style{html::raw(inst_css)} + html::div{
                                                  detail::cls("ui-sp-outer"),
                                                  html::attr.id(outer_id),
                                                  html::div{
                                                      detail::cls("ui-sp-sticky"),
                                                      html::div{
                                                          detail::cls("ui-sp-card"),
                                                          html::attr.id(card_id),
                                                          std::move(content),
                                                      },
                                                  },
                                              };
}

// ── sidebar ───────────────────────────────────────────────────────────────────
// ui::sidebar("id", sidebar_node, main_node)

inline auto sidebar(std::string_view id, html::Node sidebar_content, html::Node main_content) -> html::Node {
    const auto fid = std::format("uisb-{}", id);
    return html::div{
        detail::cls("ui-sb-root"),
        html::input{html::attr.type("checkbox"), html::attr.id(fid), detail::cls("ui-sb-cb")},
        html::label{
            html::attr.for_(fid),
            detail::cls("ui-sb-toggle"),
            html::raw("&#x2630; Toggle sidebar"),
        },
        html::div{
            detail::cls("ui-sb-wrap"),
            html::div{detail::cls("ui-sb-sidebar"), std::move(sidebar_content)},
            html::div{detail::cls("ui-sb-main"), std::move(main_content)},
        },
    };
}

// ── theme_toggle ──────────────────────────────────────────────────────────────
// ui::theme_toggle("id", init_dark, "/toggle-path")
// Sun/moon icon swap uses static sibling selector in stylesheet.
// Body CSS variables remain instance CSS (body:has(#id) must be body-scoped).

inline auto theme_toggle(std::string_view id, bool init_dark = false, std::string_view toggle_path = "",
                         std::string_view scope = "body") -> html::Node {
    const auto fid = std::format("uitm-{}", id);
    // Scoped toggles pin light as the base so the toggle is bidirectional
    // regardless of page theme. Page-level (body) inherits system default.
    std::string inst_css;
    if (scope != "body") {
        inst_css += std::format("{}{{", scope) + "--bg:#f4f1eb;--surface:#eae7e0;--surface2:#dedad2;--border:#cdc9bf;"
                                                 "--text:#1b1916;--muted:#6c6860;--accent:#2563eb;--accent-dim:#1d4ed8;"
                                                 "--accent-fg:#fff;--code-bg:#e4e0d8;--code-fg:#1e293b;"
                                                 "--ok:#166534;--ok-bg:#dcfce7;--ok-bd:#86efac;"
                                                 "--err:#991b1b;--err-bg:#fee2e2;--err-bd:#fca5a5}";
    }
    inst_css += std::format("{}:has(#{}:checked){{", scope, fid) +
                "--bg:#060b14;--surface:#0c1220;--surface2:#111927;--border:#1c2a3a;"
                "--text:#e0e8f4;--muted:#7086a0;--accent:#38bdf8;--accent-dim:#0ea5e9;"
                "--accent-fg:#001220;--code-bg:#040810;--code-fg:#7dd3fc;"
                "--ok:#4ade80;--ok-bg:#042011;--ok-bd:#166534;"
                "--err:#fca5a5;--err-bg:#3b0808;--err-bd:#991b1b}";
    const auto htmx_attrs = toggle_path.empty() ? html::Node{}
                                                : html::raw(std::format(" hx-post=\"{}\""
                                                                        " hx-trigger=\"change\" hx-swap=\"none\"",
                                                                        toggle_path));
    return html::style{html::raw(inst_css)} +
           html::input{html::attr.type("checkbox"), html::attr.id(fid), detail::cls("ui-tm-cb"),
                       init_dark ? html::attr.checked() : html::Node{}, htmx_attrs} +
           html::label{
               html::attr.for_(fid),
               detail::cls("ui-tm-label"),
               html::span{detail::cls("ui-tm-sun"), html::raw("&#x2600;&#xFE0F;")},
               html::span{detail::cls("ui-tm-moon"), html::raw("&#x1F319;")},
           };
}

// Scoped theme toggle — no server-side persistence, scope is a CSS selector wrapping the widget.
// Avoids the empty-string toggle_path workaround: ui::scoped_theme_toggle("id", false, "#wrap")
inline auto scoped_theme_toggle(std::string_view id, bool init_dark, std::string_view scope) -> html::Node {
    return theme_toggle(id, init_dark, "", scope);
}

// ── js utilities ─────────────────────────────────────────────────────────────
// Each function registers a global event-delegation handler (idempotent via emplace)
// and returns an attribute bundle that activates the behavior on the element.
// Handlers work with HTMX-swapped content because they delegate from document.
//
// Integration with HTMX search:
//   html::input{ui::js::debounce(400), htmx::attr::trigger("debounced"), hx_get("/search")}

namespace js {

// Textarea grows to fit content as user types.
inline auto auto_resize() -> html::Node {
    html::detail::ui_js_registry.emplace(
        "auto_resize", "document.addEventListener('input',e=>{if(e.target.matches('[data-autoresize]')){"
                       "e.target.style.height='auto';e.target.style.height=e.target.scrollHeight+'px'}});");
    return html::Node{html::raw_tag{}, " data-autoresize=\"\""};
}

// Shows character count (and optional max) in element with given id.
// html::textarea{ui::js::char_counter("my-count"), html::attr.maxlength("500")},
// html::span{html::attr.id("my-count"), "0"}
inline auto char_counter(std::string_view counter_id) -> html::Node {
    html::detail::ui_js_registry.emplace(
        "char_counter", "document.addEventListener('input',e=>{const t=e.target;if(!t.dataset.charCounter)return;"
                        "const el=document.getElementById(t.dataset.charCounter);if(el)"
                        "el.textContent=t.value.length+(t.maxLength>0?'/'+t.maxLength:'')});");
    return html::attr.data("char-counter", counter_id);
}

// Hides element when user clicks anywhere outside it.
// Best for pure toggle (hidden attr), not HTMX-managed open state.
inline auto click_outside() -> html::Node {
    html::detail::ui_js_registry.emplace("click_outside",
                                         "document.addEventListener('click',e=>{"
                                         "document.querySelectorAll('[data-click-outside]:not([hidden])').forEach(el=>{"
                                         "if(!el.contains(e.target))el.hidden=true})});");
    return html::Node{html::raw_tag{}, " data-click-outside=\"\""};
}

// Traps Tab focus within element (a11y for modals/dialogs).
// Add focus_trap_escape as a sibling attribute to close on Escape.
inline auto focus_trap() -> html::Node {
    html::detail::ui_js_registry.emplace(
        "focus_trap", "document.addEventListener('keydown',e=>{"
                      "const trap=document.querySelector('[data-focus-trap]:not([hidden])');"
                      "if(!trap)return;"
                      "const f=[...trap.querySelectorAll("
                      "'a[href],button:not([disabled]),input:not([disabled]),"
                      "select:not([disabled]),textarea:not([disabled]),[tabindex]:not([tabindex=\"-1\"])')"
                      "].filter(el=>!el.closest('[hidden]'));"
                      "if(!f.length)return;"
                      "const first=f[0],last=f[f.length-1];"
                      "if(e.key==='Tab'){if(e.shiftKey&&document.activeElement===first)"
                      "{e.preventDefault();last.focus()}else if(!e.shiftKey&&document.activeElement===last)"
                      "{e.preventDefault();first.focus()}}"
                      "if(e.key==='Escape'&&'focusTrapEscape'in trap.dataset)trap.hidden=true});");
    return html::Node{html::raw_tag{}, " data-focus-trap=\"\""};
}

// Pair with focus_trap() to allow Escape key to close.
inline const html::Node focus_trap_escape{html::raw_tag{}, " data-focus-trap-escape=\"\""};

// Fires a 'debounced' event after user stops typing for `ms` milliseconds.
// Use with hx-trigger="debounced" for HTMX live search without spamming requests.
inline auto debounce(int ms = 300) -> html::Node {
    html::detail::ui_js_registry.emplace(
        "debounce", "const _cw_db=new Map();"
                    "document.addEventListener('input',e=>{"
                    "const t=e.target;if(!('debounceMs'in t.dataset))return;"
                    "clearTimeout(_cw_db.get(t));"
                    "_cw_db.set(t,setTimeout(()=>t.dispatchEvent(new Event('debounced',{bubbles:true})),"
                    "+(t.dataset.debounceMs||300)))});");
    return html::attr.data("debounce-ms", std::format("{}", ms));
}

// Element is clicked when key combo fires (skipped when focus is in an input).
// Combo format: "cmd+k", "ctrl+shift+p", "escape", etc.
inline auto kbd(std::string_view combo) -> html::Node {
    html::detail::ui_js_registry.emplace("kbd", "document.addEventListener('keydown',e=>{"
                                                "if(['INPUT','TEXTAREA','SELECT'].includes(e.target.tagName))return;"
                                                "const k=(e.metaKey?'cmd+':e.ctrlKey?'ctrl+':e.altKey?'alt+':'')"
                                                "+(e.shiftKey?'shift+':'')+e.key.toLowerCase();"
                                                "document.querySelectorAll('[data-kbd]').forEach(el=>{"
                                                "if(el.dataset.kbd===k){e.preventDefault();el.click()}})});");
    return html::attr.data("kbd", combo);
}

// Triggers the browser Web Share API (mobile-first; no-op on unsupported browsers).
// Optional title/url override; defaults to document.title and current URL.
inline auto share(std::string_view title = "", std::string_view url = "") -> html::Node {
    html::detail::ui_js_registry.emplace("share", "document.addEventListener('click',e=>{"
                                                  "const t=e.target.closest('[data-share]');if(!t)return;"
                                                  "navigator.share?.({title:t.dataset.shareTitle||document.title,"
                                                  "url:t.dataset.shareUrl||location.href}).catch(()=>{})});");
    return html::fragment(html::Node{html::raw_tag{}, " data-share=\"\""},
                          title.empty() ? html::Node{} : html::attr.data("share-title", title),
                          url.empty() ? html::Node{} : html::attr.data("share-url", url));
}

// Hidden when at top of page; shown when scrolled; scrolls to top on click.
// html::button{ui::js::scroll_to_top(), "↑ Top"}
inline auto scroll_to_top() -> html::Node {
    html::detail::ui_js_registry.emplace(
        "scroll_to_top", "{const s=new IntersectionObserver(([e])=>"
                         "document.querySelectorAll('[data-scroll-top]').forEach(el=>"
                         "el.toggleAttribute('hidden',e.isIntersecting)));"
                         "const d=document.createElement('div');document.body.prepend(d);s.observe(d);"
                         "document.addEventListener('click',e=>{"
                         "if(e.target.closest('[data-scroll-top]'))window.scrollTo({top:0,behavior:'smooth'})})}");
    return html::Node{html::raw_tag{}, " data-scroll-top=\"\" hidden"};
}

// Requests geolocation and fills target input with "lat,lng".
// html::button{ui::js::geolocate("#coords"), "Use my location"}
// html::input{html::attr.id("coords"), ...}
inline auto geolocate(std::string_view target_selector) -> html::Node {
    html::detail::ui_js_registry.emplace("geolocate", "document.addEventListener('click',e=>{"
                                                      "const t=e.target.closest('[data-geolocate]');if(!t)return;"
                                                      "navigator.geolocation.getCurrentPosition(p=>{"
                                                      "const el=document.querySelector(t.dataset.geoTarget);"
                                                      "if(el)el.value=p.coords.latitude+','+p.coords.longitude},"
                                                      "{enableHighAccuracy:true})});");
    return html::fragment(html::Node{html::raw_tag{}, " data-geolocate=\"\""},
                          html::attr.data("geo-target", target_selector));
}

// Toggles fullscreen on target element (default: entire page).
// html::button{ui::js::fullscreen(), "⛶"}
// html::button{ui::js::fullscreen("#video"), "⛶"}
inline auto fullscreen(std::string_view target = "") -> html::Node {
    html::detail::ui_js_registry.emplace(
        "fullscreen", "document.addEventListener('click',e=>{"
                      "const t=e.target.closest('[data-fullscreen]');if(!t)return;"
                      "const el=t.dataset.fullscreenTarget"
                      "?document.querySelector(t.dataset.fullscreenTarget):document.documentElement;"
                      "document.fullscreenElement?document.exitFullscreen():el.requestFullscreen()});");
    return html::fragment(html::Node{html::raw_tag{}, " data-fullscreen=\"\""},
                          target.empty() ? html::Node{} : html::attr.data("fullscreen-target", target));
}

// Catches Ctrl+V image paste and populates a file input within the zone.
// html::form{ui::js::paste_zone(),
//   html::input{html::attr.type("file"), html::attr("data-paste-input","")}}
inline auto paste_zone() -> html::Node {
    html::detail::ui_js_registry.emplace(
        "image_paste", "document.addEventListener('paste',e=>{"
                       "const z=document.querySelector('[data-paste-zone]');if(!z)return;"
                       "const f=[...e.clipboardData.files].find(f=>f.type.startsWith('image/'));"
                       "if(!f)return;"
                       "const inp=z.querySelector('[data-paste-input]')||z.querySelector('input[type=\"file\"]');"
                       "if(!inp)return;"
                       "const dt=new DataTransfer();dt.items.add(f);inp.files=dt.files;"
                       "inp.dispatchEvent(new Event('change',{bubbles:true}))});");
    return html::Node{html::raw_tag{}, " data-paste-zone=\"\""};
}

} // namespace js

// ── clipboard ─────────────────────────────────────────────────────────────────
// Returns an attribute bundle (data-* + onclick) applicable to any element.
// The text/url is stored in a data attribute — never embedded in the JS — so
// no escaping or injection issues regardless of content.
//
//   html::button{ui::clipboard::text(page_url), "Copy Link"}
//   html::button{ui::clipboard::image("/avatar.png"), "Copy Image"}
//   html::span{ui::clipboard::text(code_snippet), "📋"}
//   html::img{html::attr.src(url), ui::clipboard::image(url)}

namespace clipboard {

inline auto text(std::string_view value) -> html::Node {
    html::detail::ui_js_registry.emplace("clipboard",
                                         "function _cwCopy(el){"
                                         "const txt=el.dataset.copyText;"
                                         "const ok=()=>{"
                                         "const prev=el.innerHTML;"
                                         "el.textContent='Copied!';"
                                         "clearTimeout(el._cwt);"
                                         "el._cwt=setTimeout(()=>el.innerHTML=prev,1400)"
                                         "};"
                                         "if(navigator.clipboard?.writeText){"
                                         "navigator.clipboard.writeText(txt).then(ok).catch(()=>_cwFb(txt,ok))"
                                         "}else{"
                                         "_cwFb(txt,ok)"
                                         "}"
                                         "}"
                                         "function _cwFb(txt,ok){"
                                         "const t=Object.assign(document.createElement('textarea'),"
                                         "{value:txt,style:'position:fixed;opacity:0'});"
                                         "document.body.appendChild(t);t.select();"
                                         "try{document.execCommand('copy');ok()}catch(e){}"
                                         "document.body.removeChild(t)"
                                         "}");
    return html::fragment(html::attr.data("copy-text", value), html::attr.onclick("_cwCopy(this)"));
}

inline auto image(std::string_view url) -> html::Node {
    return html::fragment(html::attr.data("copy-url", url),
                          html::attr.onclick("fetch(this.dataset.copyUrl)"
                                             ".then(r=>r.blob())"
                                             ".then(b=>navigator.clipboard.write([new ClipboardItem({[b.type]:b})]))"));
}

// Convenience: pre-built button. Use text()/image() directly for custom elements.
inline auto btn(std::string_view value, std::string_view label = "Copy") -> html::Node {
    return html::button{text(value), label};
}

inline auto image_btn(std::string_view url, std::string_view label = "Copy Image") -> html::Node {
    return html::button{image(url), label};
}

} // namespace clipboard

} // namespace ui
