#pragma once
#include "html.hpp"

namespace svg {

// SVG has no void elements: "<path d=...>" without "/>" stays open, and whatever
// follows it nests inside. So leaves always self-close.
template<html::fixed_string Tag>
struct leaf : html::Node {
    leaf(std::initializer_list<html::Node> c = {})
        : html::Node{html::raw_tag{}, std::format("<{}{}/>", Tag.value, html::detail::split(c).first)} {}

    template<typename... Args>
    leaf(Args&&... args) : leaf({html::Node(std::forward<Args>(args))...}) {}
};

// ── Attribute helpers ─────────────────────────────────────────────────────────
struct attr_t {
    constexpr auto operator()(std::string_view name, std::string_view value) const -> html::Node {
        return html::Node{html::raw_tag{}, std::format(" {}=\"{}\"", name, sanitize::html_escape(value))};
    }

    // Presentation
    constexpr auto fill(std::string_view v) const {
        return (*this)("fill", v);
    }
    constexpr auto stroke(std::string_view v) const {
        return (*this)("stroke", v);
    }
    constexpr auto stroke_width(std::string_view v) const {
        return (*this)("stroke-width", v);
    }
    constexpr auto stroke_linecap(std::string_view v) const {
        return (*this)("stroke-linecap", v);
    }
    constexpr auto stroke_linejoin(std::string_view v) const {
        return (*this)("stroke-linejoin", v);
    }
    constexpr auto fill_rule(std::string_view v) const {
        return (*this)("fill-rule", v);
    }
    constexpr auto opacity(std::string_view v) const {
        return (*this)("opacity", v);
    }

    // Dimensions & position
    constexpr auto width(std::string_view v) const {
        return (*this)("width", v);
    }
    constexpr auto height(std::string_view v) const {
        return (*this)("height", v);
    }
    constexpr auto x(std::string_view v) const {
        return (*this)("x", v);
    }
    constexpr auto y(std::string_view v) const {
        return (*this)("y", v);
    }
    constexpr auto rx(std::string_view v) const {
        return (*this)("rx", v);
    }
    constexpr auto ry(std::string_view v) const {
        return (*this)("ry", v);
    }
    constexpr auto cx(std::string_view v) const {
        return (*this)("cx", v);
    }
    constexpr auto cy(std::string_view v) const {
        return (*this)("cy", v);
    }
    constexpr auto r(std::string_view v) const {
        return (*this)("r", v);
    }
    constexpr auto x1(std::string_view v) const {
        return (*this)("x1", v);
    }
    constexpr auto y1(std::string_view v) const {
        return (*this)("y1", v);
    }
    constexpr auto x2(std::string_view v) const {
        return (*this)("x2", v);
    }
    constexpr auto y2(std::string_view v) const {
        return (*this)("y2", v);
    }

    // Path & shape data
    constexpr auto d(std::string_view v) const {
        return (*this)("d", v);
    }
    constexpr auto points(std::string_view v) const {
        return (*this)("points", v);
    }

    // Viewport & transform
    constexpr auto viewBox(std::string_view v) const {
        return (*this)("viewBox", v);
    }
    constexpr auto transform(std::string_view v) const {
        return (*this)("transform", v);
    }
    constexpr auto preserveAspectRatio(std::string_view v) const {
        return (*this)("preserveAspectRatio", v);
    }

    // Gradient
    constexpr auto offset(std::string_view v) const {
        return (*this)("offset", v);
    }
    constexpr auto stop_color(std::string_view v) const {
        return (*this)("stop-color", v);
    }
    constexpr auto stop_opacity(std::string_view v) const {
        return (*this)("stop-opacity", v);
    }
    constexpr auto gradientUnits(std::string_view v) const {
        return (*this)("gradientUnits", v);
    }

    // General
    constexpr auto id(std::string_view v) const {
        return (*this)("id", v);
    }
    constexpr auto class_(std::string_view v) const {
        return (*this)("class", v);
    }
    constexpr auto style(std::string_view v) const {
        return (*this)("style", v);
    }
    constexpr auto href(std::string_view v) const {
        return (*this)("href", v);
    }
    constexpr auto aria_label(std::string_view v) const {
        return (*this)("aria-label", v);
    }
    constexpr auto xmlns(std::string_view v) const {
        return (*this)("xmlns", v);
    }
};

inline constexpr attr_t attr;

// ── Container elements (paired open/close tags) ───────────────────────────────
using svg = html::elem<"svg">;
using g = html::elem<"g">;
using defs = html::elem<"defs">;
using symbol_el = html::elem<"symbol">;
using text = html::elem<"text">;
using tspan = html::elem<"tspan">;
using clipPath = html::elem<"clipPath">;
using mask = html::elem<"mask">;
using marker = html::elem<"marker">;
using pattern = html::elem<"pattern">;
using linearGradient = html::elem<"linearGradient">;
using radialGradient = html::elem<"radialGradient">;
using stop = html::elem<"stop">;
using title = html::elem<"title">;

// ── Leaf elements (no children) ───────────────────────────────────────────────
using path = leaf<"path">;
using circle = leaf<"circle">;
using ellipse = leaf<"ellipse">;
using rect = leaf<"rect">;
using line = leaf<"line">;
using polyline = leaf<"polyline">;
using polygon = leaf<"polygon">;
using use_el = leaf<"use">;
using image = leaf<"image">;

} // namespace svg
