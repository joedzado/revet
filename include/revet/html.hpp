#pragma once

#include "sanitize.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <format>
#include <initializer_list>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace html {

// ── compile-time string for NTTP ─────────────────────────────────────────────
template<size_t N>
struct fixed_string {
    char value[N]{};
    consteval fixed_string(const char (&s)[N]) {
        std::copy_n(s, N, value);
    }
};
template<size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

// ── base node ─────────────────────────────────────────────────────────────────
struct raw_tag {};

struct Node {
    // A child starting with a space is read as an attribute by detail::split, so text
    // must never start with one, or " onclick=x" in user data would become a live attribute.
    static constexpr auto escape_text(std::string_view s) -> std::string {
        std::string out = sanitize::html_escape(s);
        if (out.starts_with(' ')) {
            out.replace(0, 1, "&#32;");
        }
        return out;
    }

    std::string str;

    constexpr Node() = default;
    constexpr Node(const std::string& s) : str(escape_text(s)) {}
    constexpr Node(std::string_view s) : str(escape_text(s)) {}
    // Literals may use the " #id" and " .class" shorthand. Runtime strings never can.
    constexpr Node(const char* s)
        : str(std::string_view(s).starts_with(" #") || std::string_view(s).starts_with(" .") ? sanitize::html_escape(s)
                                                                                              : escape_text(s)) {}
    constexpr Node(raw_tag, std::string s) : str(std::move(s)) {}

    template<std::integral T>
        requires(!std::is_same_v<T, bool>) && (!std::is_same_v<T, char>) && (!std::is_same_v<T, signed char>) &&
                (!std::is_same_v<T, unsigned char>)
    Node(T v) : str(std::format("{}", v)) {}

    template<std::floating_point T>
    Node(T v) : str(std::format("{}", v)) {}

    constexpr operator std::string() const& {
        return str;
    }
    constexpr operator std::string() && {
        return std::move(str);
    }
};

constexpr auto operator+(Node lhs, Node rhs) -> Node {
    lhs.str += std::move(rhs.str);
    return lhs;
}

// ── Helpers ──────────────────────────────────────────────────────────────────
constexpr auto raw(std::string s) -> Node {
    return Node{raw_tag{}, std::move(s)};
}

template<typename... Args>
auto fragment(Args&&... args) -> Node {
    std::string result;
    auto append = [&result](auto&& arg) { result.append(std::string(Node(std::forward<decltype(arg)>(arg)))); };
    (append(std::forward<Args>(args)), ...);
    return Node{raw_tag{}, std::move(result)};
}

struct attr_t {
    constexpr auto operator()(std::string_view name, std::string_view value) const -> Node {
        return Node{raw_tag{}, std::format(" {}=\"{}\"", name, sanitize::html_escape(value))};
    }

    // Common Attributes
    constexpr auto id(std::string_view v) const {
        return (*this)("id", v);
    }
    constexpr auto name(std::string_view v) const {
        return (*this)("name", v);
    }
    constexpr auto type(std::string_view v) const {
        return (*this)("type", v);
    }
    constexpr auto value(std::string_view v) const {
        return (*this)("value", v);
    }
    constexpr auto placeholder(std::string_view v) const {
        return (*this)("placeholder", v);
    }
    auto href(std::string_view v) const {
        return (*this)("href", sanitize::safe_url(v));
    }
    auto src(std::string_view v) const {
        return (*this)("src", sanitize::safe_url(v));
    }
    constexpr auto for_(std::string_view v) const {
        return (*this)("for", v);
    }
    constexpr auto style(std::string_view v) const {
        return (*this)("style", v);
    }
    constexpr auto title(std::string_view v) const {
        return (*this)("title", v);
    }
    constexpr auto width(std::string_view v) const {
        return (*this)("width", v);
    }
    constexpr auto height(std::string_view v) const {
        return (*this)("height", v);
    }
    constexpr auto alt(std::string_view v) const {
        return (*this)("alt", v);
    }
    constexpr auto loading(std::string_view v) const {
        return (*this)("loading", v);
    }
    constexpr auto rel(std::string_view v) const {
        return (*this)("rel", v);
    }
    constexpr auto target(std::string_view v) const {
        return (*this)("target", v);
    }
    constexpr auto charset(std::string_view v) const {
        return (*this)("charset", v);
    }
    constexpr auto content(std::string_view v) const {
        return (*this)("content", v);
    }
    constexpr auto method(std::string_view v) const {
        return (*this)("method", v);
    }
    constexpr auto action(std::string_view v) const {
        return (*this)("action", v);
    }
    constexpr auto autocomplete(std::string_view v) const {
        return (*this)("autocomplete", v);
    }
    constexpr auto enctype(std::string_view v) const {
        return (*this)("enctype", v);
    }
    constexpr auto accept(std::string_view v) const {
        return (*this)("accept", v);
    }
    constexpr auto min(std::string_view v) const {
        return (*this)("min", v);
    }
    constexpr auto max(std::string_view v) const {
        return (*this)("max", v);
    }
    constexpr auto maxlength(std::string_view v) const {
        return (*this)("maxlength", v);
    }
    constexpr auto step(std::string_view v) const {
        return (*this)("step", v);
    }
    constexpr auto class_(std::string_view v) const {
        return (*this)("class", v);
    }
    auto data(std::string_view key, std::string_view value) const {
        return (*this)(std::format("data-{}", key), value);
    }

    // Fixed-value attributes — pre-fill common single-value attrs so callers never pass them as strings.
    constexpr auto post() const {
        return Node{raw_tag{}, " method=\"POST\""};
    }
    constexpr auto multipart() const {
        return Node{raw_tag{}, " enctype=\"multipart/form-data\""};
    }
    constexpr auto accept_images() const {
        return Node{raw_tag{}, " accept=\"image/*\""};
    }
    constexpr auto accept_videos() const {
        return Node{raw_tag{}, " accept=\"video/*\""};
    }
    constexpr auto accept_audio() const {
        return Node{raw_tag{}, " accept=\"audio/*\""};
    }
    constexpr auto accept_pdf() const {
        return Node{raw_tag{}, " accept=\".pdf\""};
    }

    // Boolean Attributes
    constexpr auto required() const {
        return Node{raw_tag{}, " required"};
    }
    constexpr auto disabled() const {
        return Node{raw_tag{}, " disabled"};
    }
    constexpr auto readonly() const {
        return Node{raw_tag{}, " readonly"};
    }
    constexpr auto checked() const {
        return Node{raw_tag{}, " checked"};
    }
    constexpr auto selected() const {
        return Node{raw_tag{}, " selected"};
    }
    constexpr auto multiple() const {
        return Node{raw_tag{}, " multiple"};
    }
    constexpr auto autofocus() const {
        return Node{raw_tag{}, " autofocus"};
    }
    constexpr auto hidden() const {
        return Node{raw_tag{}, " hidden"};
    }
    constexpr auto novalidate() const {
        return Node{raw_tag{}, " novalidate"};
    }

    // Events
    constexpr auto onclick(std::string_view v) const {
        return (*this)("onclick", v);
    }
    constexpr auto oninput(std::string_view v) const {
        return (*this)("oninput", v);
    }
    constexpr auto onchange(std::string_view v) const {
        return (*this)("onchange", v);
    }
    constexpr auto onsubmit(std::string_view v) const {
        return (*this)("onsubmit", v);
    }
};

inline constexpr attr_t attr;

// Type-safe element ID. Place directly in element children to set id="name",
// or call .for_() to produce a matching for="name" attribute.
//   static constexpr html::id MY_ID{"foo"};
//   html::div{MY_ID, ...}           → <div id="foo">
//   html::label{MY_ID.for_(), ...}  → <label for="foo">
struct id {
    std::string_view name;
    constexpr explicit id(std::string_view n) : name(n) {}
    constexpr operator Node() const {
        return Node{raw_tag{}, std::format(" #{}", name)};
    }
    constexpr auto for_() const -> Node {
        return attr.for_(name);
    }
    // Returns "#name" for use in CSS selectors (htmx targets, querySelector, etc.)
    auto selector() const -> std::string {
        return std::format("#{}", name);
    }
};

// typed_id<T> — like id, but records the C++ type of the element's text content.
// Enables type-safe optimistic updates via htmx::optimistic(id, delta).
//   static constexpr html::typed_id<int>    k_count{"count"};
//   static constexpr html::typed_id<double> k_price{"price"};
//   static constexpr html::typed_id<bool>   k_liked{"liked-btn", "active"};
//   html::div{k_count, styles::counter_value, val}   → <div id="count">N</div>
//   htmx::optimistic(k_count, +1)                    → type-checked, correct JS
//   htmx::optimistic(k_liked)                        → toggles class "active"
template<typename T>
struct typed_id {
    std::string_view name;
    constexpr explicit typed_id(std::string_view n) : name(n) {}
    constexpr operator Node() const {
        return Node{raw_tag{}, std::format(" #{}", name)};
    }
    constexpr auto for_() const -> Node {
        return attr.for_(name);
    }
    auto selector() const -> std::string {
        return std::format("#{}", name);
    }
};

template<>
struct typed_id<bool> {
    std::string_view name;
    std::string_view cls;
    constexpr explicit typed_id(std::string_view n, std::string_view c) : name(n), cls(c) {}
    constexpr operator Node() const {
        return Node{raw_tag{}, std::format(" #{}", name)};
    }
    constexpr auto for_() const -> Node {
        return attr.for_(name);
    }
    auto selector() const -> std::string {
        return std::format("#{}", name);
    }
};

// ── tag generation ────────────────────────────────────────────────────────────
namespace detail {
constexpr auto split(std::initializer_list<Node> children) -> std::pair<std::string, std::string> {
    std::string attrs, classes, content, id;
    for (const auto& c : children) {
        if (c.str.size() > 1 && c.str.at(0) == ' ' && c.str.at(1) == '.') {
            classes += (classes.empty() ? "" : " ") + c.str.substr(2);
        } else if (c.str.size() > 1 && c.str.at(0) == ' ' && c.str.at(1) == '#') {
            id = c.str.substr(2);
        } else if (c.str.starts_with(' ')) {
            attrs += c.str;
        } else {
            content += c.str;
        }
    }
    if (!classes.empty()) {
        attrs = " class=\"" + classes + "\"" + attrs;
    }
    if (!id.empty()) {
        attrs = " id=\"" + id + "\"" + attrs;
    }
    return {attrs, content};
}

constexpr auto tag(std::string_view name, std::initializer_list<Node> children) -> std::string {
    auto [attrs, content] = split(children);
    return std::format("<{}{}>{}</{}>", name, attrs, content, name);
}

constexpr auto void_tag(std::string_view name, std::initializer_list<Node> children) -> std::string {
    auto [attrs, _] = split(children);
    return std::format("<{}{}>", name, attrs);
}
} // namespace detail

// ── components ────────────────────────────────────────────────────────────────
template<fixed_string Tag>
struct elem : Node {
    constexpr elem(std::initializer_list<Node> c) : Node{raw_tag{}, detail::tag(Tag.value, c)} {}

    template<typename... Args>
    constexpr elem(Args&&... args) : elem({Node(std::forward<Args>(args))...}) {}
};

template<fixed_string Tag>
struct void_elem : Node {
    constexpr void_elem(std::initializer_list<Node> c = {}) : Node{raw_tag{}, detail::void_tag(Tag.value, c)} {}

    template<typename... Args>
    constexpr void_elem(Args&&... args) : void_elem({Node(std::forward<Args>(args))...}) {}
};

// ── elements ──────────────────────────────────────────────────────────────────
using div = elem<"div">;
using span = elem<"span">;
using p = elem<"p">;
using h1 = elem<"h1">;
using h2 = elem<"h2">;
using h3 = elem<"h3">;
using h4 = elem<"h4">;
using ul = elem<"ul">;
using ol = elem<"ol">;
using li = elem<"li">;
using a = elem<"a">;
using em = elem<"em">;
using strong = elem<"strong">;
using pre = elem<"pre">;
using code = elem<"code">;
using form = elem<"form">;
using label = elem<"label">;
using select = elem<"select">;
using option = elem<"option">;
using textarea = elem<"textarea">;
using button = elem<"button">;
using input = void_elem<"input">;

// Typed input variants — pre-inject type="..." so callers never pass it as a string.
struct checkbox : void_elem<"input"> {
    template<typename... Args>
    constexpr checkbox(Args&&... args)
        : void_elem<"input">(Node{raw_tag{}, " type=\"checkbox\""}, std::forward<Args>(args)...) {}
};
struct submit : elem<"button"> {
    template<typename... Args>
    constexpr submit(Args&&... args)
        : elem<"button">(Node{raw_tag{}, " type=\"submit\""}, std::forward<Args>(args)...) {}
};
struct file_input : void_elem<"input"> {
    template<typename... Args>
    constexpr file_input(Args&&... args)
        : void_elem<"input">(Node{raw_tag{}, " type=\"file\""}, std::forward<Args>(args)...) {}
};
struct search_input : void_elem<"input"> {
    template<typename... Args>
    constexpr search_input(Args&&... args)
        : void_elem<"input">(Node{raw_tag{}, " type=\"search\""}, std::forward<Args>(args)...) {}
};
using br = void_elem<"br">;
using hr = void_elem<"hr">;
using head = elem<"head">;
using body = elem<"body">;
using title = elem<"title">;
using style = elem<"style">;
using script = elem<"script">;
using meta = void_elem<"meta">;
using link = void_elem<"link">;
using header = elem<"header">;
using footer = elem<"footer">;
using nav = elem<"nav">;
using section = elem<"section">;
using article = elem<"article">;
using aside = elem<"aside">;
using table = elem<"table">;
using thead = elem<"thead">;
using tbody = elem<"tbody">;
using tfoot = elem<"tfoot">;
using tr = elem<"tr">;
using th = elem<"th">;
using td = elem<"td">;
using main_el = elem<"main">;
using section_el = section;
using figure = elem<"figure">;
using figcaption = elem<"figcaption">;
using img = void_elem<"img">;
using details = elem<"details">;
using summary = elem<"summary">;
using dialog = elem<"dialog">;
using kbd = elem<"kbd">;
using abbr = elem<"abbr">;
using mark = elem<"mark">;
using small_ = elem<"small">;
using del_ = elem<"del">;
using ins = elem<"ins">;
using blockquote = elem<"blockquote">;
using address = elem<"address">;

// ── list ──────────────────────────────────────────────────────────────────────
template<std::ranges::range Range, typename Fn>
auto list(Range&& range, Fn&& fn) -> Node {
    std::string result;
    for (auto&& item : std::forward<Range>(range)) {
        result += static_cast<std::string>(std::invoke(std::forward<Fn>(fn), item));
    }
    return raw(std::move(result));
}

template<typename Element, std::ranges::range Range>
auto list(Range&& range) -> Node {
    return list(std::forward<Range>(range),
                [](auto&& item) -> Node { return Element{std::forward<decltype(item)>(item)}; });
}

// ── classes ───────────────────────────────────────────────────────────────────
template<typename... Args>
auto classes(Args&&... args) -> Node {
    std::string result;
    auto add = [&result](std::string_view s) {
        if (!s.empty()) {
            if (!result.empty()) {
                result += " ";
            }
            result += s;
        }
    };
    (add(std::forward<Args>(args)), ...);
    return raw(std::format(" .{}", result));
}

} // namespace html
