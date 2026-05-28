#pragma once

#include "html.hpp"

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace html {

struct css_props {
    std::string_view display, position, top, right, bottom, left;
    std::string_view width, height, min_width, min_height, max_width, max_height;
    std::string_view margin, margin_top, margin_right, margin_bottom, margin_left;
    std::string_view padding, padding_top, padding_right, padding_bottom, padding_left;
    std::string_view box_sizing, overflow, overflow_x, overflow_y;
    std::string_view flex, flex_direction, flex_wrap, flex_flow;
    std::string_view flex_grow, flex_shrink, flex_basis;
    std::string_view grid, grid_template, grid_template_columns, grid_template_rows;
    std::string_view grid_gap, grid_column, grid_row, grid_area;
    std::string_view justify_content, justify_items, justify_self;
    std::string_view align_content, align_items, align_self;
    std::string_view gap, row_gap, column_gap;
    std::string_view border, border_width, border_style, border_color, border_radius;
    std::string_view background, background_color, background_image, background_size, background_position;
    std::string_view color, font_family, font_size, font_weight, line_height, text_align, text_decoration;
    std::string_view opacity, visibility, cursor, z_index, pointer_events;
    std::string_view transition, transform, animation;
    std::string_view list_style, list_style_type, list_style_position;
    std::string_view table_layout, border_collapse, border_spacing;
    std::string_view content, counter_increment, counter_reset;
    std::string_view will_change, contain;

    [[nodiscard]] auto to_string() const -> std::string {
        std::string s;
        auto emit = [&](std::string_view prop, std::string_view val) -> void {
            if (!val.empty()) {
                s += prop;
                s += ':';
                s += val;
                s += ';';
            }
        };
        emit("display", display);
        emit("position", position);
        emit("top", top);
        emit("right", right);
        emit("bottom", bottom);
        emit("left", left);
        emit("width", width);
        emit("height", height);
        emit("min-width", min_width);
        emit("min-height", min_height);
        emit("max-width", max_width);
        emit("max-height", max_height);
        emit("margin", margin);
        emit("margin-top", margin_top);
        emit("margin-right", margin_right);
        emit("margin-bottom", margin_bottom);
        emit("margin-left", margin_left);
        emit("padding", padding);
        emit("padding-top", padding_top);
        emit("padding-right", padding_right);
        emit("padding-bottom", padding_bottom);
        emit("padding-left", padding_left);
        emit("box-sizing", box_sizing);
        emit("overflow", overflow);
        emit("overflow-x", overflow_x);
        emit("overflow-y", overflow_y);
        emit("flex", flex);
        emit("flex-direction", flex_direction);
        emit("flex-wrap", flex_wrap);
        emit("flex-grow", flex_grow);
        emit("flex-shrink", flex_shrink);
        emit("flex-basis", flex_basis);
        emit("grid", grid);
        emit("grid-template", grid_template);
        emit("grid-template-columns", grid_template_columns);
        emit("grid-template-rows", grid_template_rows);
        emit("grid-gap", grid_gap);
        emit("grid-column", grid_column);
        emit("grid-row", grid_row);
        emit("grid-area", grid_area);
        emit("justify-content", justify_content);
        emit("justify-items", justify_items);
        emit("justify-self", justify_self);
        emit("align-content", align_content);
        emit("align-items", align_items);
        emit("align-self", align_self);
        emit("gap", gap);
        emit("row-gap", row_gap);
        emit("column-gap", column_gap);
        emit("border", border);
        emit("border-width", border_width);
        emit("border-style", border_style);
        emit("border-color", border_color);
        emit("border-radius", border_radius);
        emit("background", background);
        emit("background-color", background_color);
        emit("background-image", background_image);
        emit("background-size", background_size);
        emit("background-position", background_position);
        emit("color", color);
        emit("font-family", font_family);
        emit("font-size", font_size);
        emit("font-weight", font_weight);
        emit("line-height", line_height);
        emit("text-align", text_align);
        emit("text-decoration", text_decoration);
        emit("opacity", opacity);
        emit("visibility", visibility);
        emit("cursor", cursor);
        emit("z-index", z_index);
        emit("pointer-events", pointer_events);
        emit("transition", transition);
        emit("transform", transform);
        emit("animation", animation);
        emit("list-style", list_style);
        emit("list-style-type", list_style_type);
        emit("list-style-position", list_style_position);
        emit("table-layout", table_layout);
        emit("border-collapse", border_collapse);
        emit("border-spacing", border_spacing);
        emit("content", content);
        emit("counter-increment", counter_increment);
        emit("counter-reset", counter_reset);
        emit("will-change", will_change);
        emit("contain", contain);
        return s;
    }
};

namespace detail {
// Accumulates per-request dynamic classes. Thread-local so concurrent requests
// don't mix class registrations. Flushed into <style> by html_doc::render().
inline thread_local std::unordered_map<std::string, std::string> style_registry;

// ui:: component CSS/JS — keyed by component name, value is static string_view.
// Thread-local but idempotent: same key always has the same value.
inline thread_local std::unordered_map<std::string_view, std::string_view> ui_css_registry;
inline thread_local std::unordered_map<std::string_view, std::string_view> ui_js_registry;

// Pre-built CSS injected before dynamic classes (populated by the generated styles.hpp).
// Not thread-local — written once at startup and read-only thereafter.
inline std::string all_css_block;

constexpr auto fnv1a(std::string_view s) -> uint32_t {
    uint32_t h = 2166136261U;
    for (auto c : s) {
        h = (h ^ static_cast<uint8_t>(c)) * 16777619U;
    }
    return h;
}

inline auto make_class_name(std::string_view rules) -> std::string {
    uint32_t h = fnv1a(rules);
    std::array<char, 10> buf{};
    buf[0] = 'c';
    buf[1] = 'w';
    buf[2] = '-';
    for (int i = 8; i >= 3; --i) {
        buf[static_cast<size_t>(i)] = "0123456789abcdef"[h & 0xf];
        h >>= 4;
    }
    buf[9] = '\0';
    return buf.data();
}
} // namespace detail

struct css_class {
    std::string rules;
    std::string name;
    std::vector<std::pair<std::string, std::string>> pseudo;

    css_class(std::string_view raw) : rules(raw), name(detail::make_class_name(raw)) {}
    css_class(const css_props& p) : rules(p.to_string()), name(detail::make_class_name(rules)) {}

    [[nodiscard]] auto hover(const css_props& p) const -> css_class {
        return _pseudo(":hover", p.to_string());
    }
    [[nodiscard]] auto focus(const css_props& p) const -> css_class {
        return _pseudo(":focus", p.to_string());
    }
    [[nodiscard]] auto active(const css_props& p) const -> css_class {
        return _pseudo(":active", p.to_string());
    }
    [[nodiscard]] auto pseudo_sel(std::string_view sel, std::string_view raw) const -> css_class {
        return _pseudo(std::string(sel), std::string(raw));
    }
    [[nodiscard]] auto extra(std::string_view raw) const -> css_class {
        css_class c = *this;
        c.rules += raw;
        c.name = detail::make_class_name(c.rules);
        return c;
    }

    operator Node() const {
        detail::style_registry[name] = rules;
        for (const auto& [sel, r] : pseudo) {
            detail::style_registry[name + sel] = r;
        }
        return Node{raw_tag{}, std::format(" .{}", name)};
    }

private:
    [[nodiscard]] auto _pseudo(std::string sel, std::string r) const -> css_class {
        css_class c = *this;
        c.pseudo.emplace_back(std::move(sel), std::move(r));
        return c;
    }
};

// Represents a static CSS class defined in styles/*.css.
// Supports .extra() to append dynamic inline rules to a derived class.
struct named_class {
    std::string_view css_name;

    [[nodiscard]] auto extra(std::string_view rules) const -> Node {
        std::string dyn_name = detail::make_class_name(rules);
        detail::style_registry[dyn_name] = std::string(rules);
        // " .sec-label cw-abc123" → split() strips " ." prefix → "sec-label cw-abc123" → class="sec-label cw-abc123"
        return Node{raw_tag{}, std::format(" .{} {}", css_name, dyn_name)};
    }

    operator Node() const {
        return Node{raw_tag{}, std::format(" .{}", css_name)};
    }
};

// Sentinel placed in the HTML tree to mark where the <style> block is injected.
// html_doc::render() replaces it with the collected CSS; if absent, falls back
// to prepending into <head> (or the document root).
static constexpr std::string_view STYLES_SENTINEL = "<!--__REVET_STYLES__-->";

inline auto collect_styles() -> Node {
    return Node{raw_tag{}, std::string(STYLES_SENTINEL)};
}

struct html_doc : Node {
    std::string inner;

    html_doc(std::initializer_list<Node> c) : Node{raw_tag{}, ""} {
        for (const auto& n : c) {
            inner += n.str;
        }
        str = render();
    }

    [[nodiscard]] auto render() const -> std::string {
        std::string css;
        css.reserve(50 + detail::all_css_block.size() + detail::ui_css_registry.size() * 100 +
                    detail::style_registry.size() * 150);
        css += "<style>";
        css += detail::all_css_block;
        for (const auto& [k, rules] : detail::ui_css_registry) {
            css += rules;
        }
        for (const auto& [name, rules] : detail::style_registry) {
            css += '.';
            css += name;
            css += '{';
            css += rules;
            css += '}';
        }
        css += "</style>";

        std::string out = "<!DOCTYPE html><html>" + inner + "</html>";
        auto pos = out.find(STYLES_SENTINEL);
        if (pos != std::string::npos) {
            out.replace(pos, STYLES_SENTINEL.size(), css);
        } else if (size_t head = out.find("<head>"); head != std::string::npos) {
            out.insert(head + 6, css);
        } else {
            out.insert(0, css);
        }

        std::string js;
        for (const auto& [k, snippet] : detail::ui_js_registry) {
            js += snippet;
        }
        if (!js.empty()) {
            const auto script = "<script>" + js + "</script>";
            const auto body = out.rfind("</body>");
            if (body != std::string::npos) {
                out.insert(body, script);
            } else {
                out += script;
            }
        }
        detail::style_registry.clear();
        detail::ui_css_registry.clear();
        detail::ui_js_registry.clear();
        return out;
    }
};

} // namespace html
