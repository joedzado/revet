#pragma once

#include "web.hpp"

#include <chrono>
#include <string_view>

namespace htmx {

// ── HTML DSL Attribute Helpers ────────────────────────────────────────────
// All delegate to html::attr() for consistent escaping and leading-space protocol

namespace attr {
// HTTP method targets
inline auto get(std::string_view url) -> html::Node {
    return html::attr("hx-get", url);
}
inline auto post(std::string_view url) -> html::Node {
    return html::attr("hx-post", url);
}
inline auto put(std::string_view url) -> html::Node {
    return html::attr("hx-put", url);
}
inline auto del(std::string_view url) -> html::Node {
    return html::attr("hx-delete", url);
}
inline auto patch(std::string_view url) -> html::Node {
    return html::attr("hx-patch", url);
}

// Swap targets and strategies
inline auto target(std::string_view sel) -> html::Node {
    return html::attr("hx-target", sel);
}
inline auto target(const html::id& id) -> html::Node {
    return html::attr("hx-target", id.selector());
}
template<typename T>
inline auto target(const html::typed_id<T>& id) -> html::Node {
    return html::attr("hx-target", id.selector());
}
inline auto swap(std::string_view strategy) -> html::Node {
    return html::attr("hx-swap", strategy);
}
inline auto swap_outer_html() -> html::Node {
    return swap("outerHTML");
}
inline auto swap_inner_html() -> html::Node {
    return swap("innerHTML");
}
inline auto swap_before_begin() -> html::Node {
    return swap("beforebegin");
}
inline auto swap_after_begin() -> html::Node {
    return swap("afterbegin");
}
inline auto swap_before_end() -> html::Node {
    return swap("beforeend");
}
inline auto swap_after_end() -> html::Node {
    return swap("afterend");
}
inline auto swap_delete() -> html::Node {
    return swap("delete");
}
inline auto select(std::string_view sel) -> html::Node {
    return html::attr("hx-select", sel);
}
inline auto select(const html::id& id) -> html::Node {
    return html::attr("hx-select", id.selector());
}

// Event triggers
inline auto trigger(std::string_view event) -> html::Node {
    return html::attr("hx-trigger", event);
}
// trigger_input: fires on input+change, with optional debounce delay
inline auto trigger_input(std::chrono::milliseconds debounce = {}) -> html::Node {
    if (debounce.count() == 0) {
        return trigger("input changed");
    }
    return trigger(std::format("input changed delay:{}ms", debounce.count()));
}
inline auto confirm(std::string_view message) -> html::Node {
    return html::attr("hx-confirm", message);
}

// History management
inline auto push_url(std::string_view url) -> html::Node {
    return html::attr("hx-push-url", url);
}
inline auto replace_url(std::string_view url) -> html::Node {
    return html::attr("hx-replace-url", url);
}

// Misc
inline auto boost() -> html::Node {
    return html::attr("hx-boost", "true");
}
inline auto vals(std::string_view json) -> html::Node {
    return html::attr("hx-vals", json);
}
inline auto include(std::string_view sel) -> html::Node {
    return html::attr("hx-include", sel);
}
inline auto indicator(std::string_view sel) -> html::Node {
    return html::attr("hx-indicator", sel);
}
inline auto swap_oob(std::string_view strategy = "true") -> html::Node {
    return html::attr("hx-swap-oob", strategy);
}
inline auto params(std::string_view p) -> html::Node {
    return html::attr("hx-params", p);
}
inline auto sync(std::string_view s) -> html::Node {
    return html::attr("hx-sync", s);
}
inline auto ext(std::string_view e) -> html::Node {
    return html::attr("hx-ext", e);
}
inline auto headers(std::string_view h) -> html::Node {
    return html::attr("hx-headers", h);
}
inline auto disinherit(std::string_view d) -> html::Node {
    return html::attr("hx-disinherit", d);
}
inline auto encoding(std::string_view e) -> html::Node {
    return html::attr("hx-encoding", e);
}
inline auto on(std::string_view event, std::string_view code) -> html::Node {
    return html::attr(std::format("hx-on:{}", event), code);
}
inline auto validate() -> html::Node {
    return html::attr("hx-validate", "true");
}

// History & preservation
inline auto preserve() -> html::Node {
    return html::attr("hx-preserve", "true");
}
inline auto history(std::string_view v) -> html::Node {
    return html::attr("hx-history", v);
}
inline auto history_elt() -> html::Node {
    return html::attr("hx-history-elt", "true");
}

// Element state
inline auto disabled_elt(std::string_view sel) -> html::Node {
    return html::attr("hx-disabled-elt", sel);
}
inline auto select_oob(std::string_view sel) -> html::Node {
    return html::attr("hx-select-oob", sel);
}
} // namespace attr

// ── Optimistic Updates ────────────────────────────────────────────────────
// Fires before the HTMX request; server fragment confirms or corrects on swap.
//
// Three overloads:
//   optimistic(typed_id<T>, delta)    — shorthand for integral increment/decrement
//   optimistic(typed_id<T>, op)       — any JsMappable op (built-in or user-defined)
//   optimistic(id,          js)       — untyped fallback: raw JS with `e` pre-bound
//
// JsMappable concept — implement to_js(element_id) to create custom operations:
//
//   struct ClampAdd {
//       int amount, max;
//       auto to_js(std::string_view id) const -> std::string {
//           return std::format(
//               "let e=document.getElementById('{}'),v=parseInt(e.textContent,10);"
//               "e.textContent=Math.min(v+{},{})", id, amount, max);
//       }
//   };
//   htmx::optimistic(k_count, ClampAdd{.amount=1, .max=100})

template<typename Op>
concept JsMappable = requires(const Op& op, std::string_view id) {
    { op.to_js(id) } -> std::convertible_to<std::string>;
};

// JsRollbackable — extends JsMappable with a rollback_js(id) method.
// Built-in ops implement both; user-defined ops may implement either.
// When present, optimistic() automatically emits hx-on:htmx:send-error.
template<typename Op>
concept JsRollbackable = JsMappable<Op> && requires(const Op& op, std::string_view id) {
    { op.rollback_js(id) } -> std::convertible_to<std::string>;
};

// ── Built-in JsMappable operations (htmx::js::*) ─────────────────────────

namespace js {
template<typename T = int>
    requires std::integral<T> || std::floating_point<T>
struct increment {
    T by = 1;
    auto to_js(std::string_view id) const -> std::string {
        if constexpr (std::floating_point<T>) {
            return std::format("revet.opt.incrFloat('{}',{})", id, by);
        } else {
            return std::format("revet.opt.incrInt('{}',{})", id, by);
        }
    }
    auto rollback_js(std::string_view id) const -> std::string {
        return std::format("revet.opt.restoreText('{}')", id);
    }
};

template<typename T = int>
    requires std::integral<T> || std::floating_point<T>
struct decrement {
    T by = 1;
    auto to_js(std::string_view id) const -> std::string {
        if constexpr (std::floating_point<T>) {
            return std::format("revet.opt.decrFloat('{}',{})", id, by);
        } else {
            return std::format("revet.opt.decrInt('{}',{})", id, by);
        }
    }
    auto rollback_js(std::string_view id) const -> std::string {
        return std::format("revet.opt.restoreText('{}')", id);
    }
};

struct set_text {
    std::string_view value;
    auto to_js(std::string_view id) const -> std::string {
        return std::format("revet.opt.setText('{}','{}')", id, value);
    }
    auto rollback_js(std::string_view id) const -> std::string {
        return std::format("revet.opt.restoreText('{}')", id);
    }
};

struct toggle_class {
    std::string_view cls;
    auto to_js(std::string_view id) const -> std::string {
        return std::format("revet.opt.toggleClass('{}','{}')", id, cls);
    }
    auto rollback_js(std::string_view id) const -> std::string {
        return std::format("revet.opt.rollbackClass('{}','{}')", id, cls);
    }
};

struct add_class {
    std::string_view cls;
    auto to_js(std::string_view id) const -> std::string {
        return std::format("revet.opt.addClass('{}','{}')", id, cls);
    }
    auto rollback_js(std::string_view id) const -> std::string {
        return std::format("revet.opt.removeClass('{}','{}')", id, cls);
    }
};

struct remove_class {
    std::string_view cls;
    auto to_js(std::string_view id) const -> std::string {
        return std::format("revet.opt.removeClass('{}','{}')", id, cls);
    }
    auto rollback_js(std::string_view id) const -> std::string {
        return std::format("revet.opt.addClass('{}','{}')", id, cls);
    }
};
} // namespace js

// ── optimistic() overloads ────────────────────────────────────────────────

// JsMappable op: optimistic(k_count, htmx::js::increment{.by=5})
//                optimistic(k_count, ClampAdd{.amount=1, .max=100})
// Emits hx-on:htmx:send-error rollback automatically if Op is JsRollbackable.
template<typename T, JsMappable Op>
inline auto optimistic(const html::typed_id<T>& id, const Op& op) -> html::Node {
    if constexpr (JsRollbackable<Op>) {
        return html::fragment(attr::on("htmx:before-request", op.to_js(id.name)),
                              attr::on("htmx:send-error", op.rollback_js(id.name)));
    } else {
        return attr::on("htmx:before-request", op.to_js(id.name));
    }
}

// Shorthand delta: optimistic(k_count, +1) / optimistic(k_price, +0.5)
template<typename T>
    requires std::integral<T> || std::floating_point<T>
inline auto optimistic(const html::typed_id<T>& id, T delta) -> html::Node {
    if (delta >= T{0}) {
        return optimistic(id, js::increment<T>{delta});
    }
    return optimistic(id, js::decrement<T>{-delta});
}

// Bool shorthand: toggles a CSS class; rolls back on network error.
inline auto optimistic(const html::typed_id<bool>& id) -> html::Node {
    const js::toggle_class op{id.cls};
    return html::fragment(attr::on("htmx:before-request", op.to_js(id.name)),
                          attr::on("htmx:send-error", op.rollback_js(id.name)));
}

// Untyped fallback: raw JS with `e` pre-bound to the element. No auto-rollback.
//   optimistic(k_btn, "e.classList.toggle('active')")
inline auto optimistic(const html::id& id, std::string_view js) -> html::Node {
    return attr::on("htmx:before-request", std::format("let e=document.getElementById('{}');{}", id.name, js));
}

// ── Container optimistic updates ──────────────────────────────────────────
// Inject HTML into a container before the request; remove it on network error.
// The server's response fragment is the authoritative final state on success.
//
// Usage:
//   static constexpr html::id k_feed{"message-feed"};
//   htmx::optimistic_append(k_feed, html::div{styles::bubble, "Sending…"})
//
// Pending nodes carry data-optimistic-pending and are removed on send-error.

namespace detail {
inline auto js_escape(std::string_view s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\'') {
            out += "\\'";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    return out;
}
} // namespace detail

inline auto optimistic_append(const html::id& container, html::Node content) -> html::Node {
    const auto html_str = detail::js_escape(std::string(std::move(content)));
    return html::fragment(
        attr::on("htmx:before-request", std::format("revet.opt.appendPending('{}','{}')", container.name, html_str)),
        attr::on("htmx:send-error", std::format("revet.opt.removePending('{}')", container.name)));
}

inline auto optimistic_prepend(const html::id& container, html::Node content) -> html::Node {
    const auto html_str = detail::js_escape(std::string(std::move(content)));
    return html::fragment(
        attr::on("htmx:before-request", std::format("revet.opt.prependPending('{}','{}')", container.name, html_str)),
        attr::on("htmx:send-error", std::format("revet.opt.removePending('{}')", container.name)));
}

// ── Request Detection & Header Reading ────────────────────────────────────

inline auto is_request(const web::Request& req) -> bool {
    return req.headers.is("hx-request");
}

inline auto get_target(const web::Request& req) -> std::string {
    return req.headers.get("hx-target");
}

inline auto get_trigger(const web::Request& req) -> std::string {
    return req.headers.get("hx-trigger");
}

inline auto get_trigger_name(const web::Request& req) -> std::string {
    return req.headers.get("hx-trigger-name");
}

inline auto get_current_url(const web::Request& req) -> std::string {
    return req.headers.get("hx-current-url");
}

inline auto is_boosted(const web::Request& req) -> bool {
    return req.headers.is("hx-boosted");
}

// ── Response Header Helpers ───────────────────────────────────────────────

inline auto redirect(web::Response& resp, std::string_view url) -> web::Response& {
    resp.headers["hx-redirect"] = url;
    return resp;
}

inline auto reswap(web::Response& resp, std::string_view strategy) -> web::Response& {
    resp.headers["hx-reswap"] = strategy;
    return resp;
}

inline auto retarget(web::Response& resp, std::string_view selector) -> web::Response& {
    resp.headers["hx-retarget"] = selector;
    return resp;
}

inline auto trigger(web::Response& resp, std::string_view event) -> web::Response& {
    resp.headers["hx-trigger"] = event;
    return resp;
}

inline auto trigger_with_data(web::Response& resp, std::string_view json) -> web::Response& {
    resp.headers["hx-trigger"] = json;
    return resp;
}

inline auto refresh(web::Response& resp) -> web::Response& {
    resp.headers["hx-refresh"] = "true";
    return resp;
}

inline auto push_url(web::Response& resp, std::string_view url) -> web::Response& {
    resp.headers["hx-push-url"] = url;
    return resp;
}

inline auto replace_url(web::Response& resp, std::string_view url) -> web::Response& {
    resp.headers["hx-replace-url"] = url;
    return resp;
}

inline auto location(web::Response& resp, std::string_view url) -> web::Response& {
    resp.headers["hx-location"] = url;
    return resp;
}

inline auto stop_polling(web::Response& resp) -> web::Response& {
    resp.headers["hx-stop-polling"] = "true";
    return resp;
}

namespace sse {
inline auto connect(std::string_view url) -> html::Node {
    return html::attr("sse-connect", url);
}
inline auto swap(std::string_view target) -> html::Node {
    return html::attr("sse-swap", target);
}
} // namespace sse

// ── Script tags ───────────────────────────────────────────────────────────────
// Drop into html::head{} instead of CDN <script> tags.
inline auto script_tag() -> html::Node {
    return html::script{html::attr.src("/_revet/htmx.js")};
}

// Required when using htmx::optimistic_* or any htmx::js::* op.
inline auto optimistic_script_tag() -> html::Node {
    return html::script{html::attr.src("/_revet/optimistic.js")};
}

} // namespace htmx
