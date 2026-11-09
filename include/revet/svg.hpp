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
    constexpr auto operator()(std::string_view name, const html::attr_value& value) const -> html::Node {
        return html::Node{html::raw_tag{}, std::format(" {}=\"{}\"", name, sanitize::html_escape(value.s))};
    }

    // Presentation
    constexpr auto fill(const html::attr_value& v) const {
        return (*this)("fill", v);
    }
    constexpr auto stroke(const html::attr_value& v) const {
        return (*this)("stroke", v);
    }
    constexpr auto stroke_width(const html::attr_value& v) const {
        return (*this)("stroke-width", v);
    }
    constexpr auto stroke_linecap(const html::attr_value& v) const {
        return (*this)("stroke-linecap", v);
    }
    constexpr auto stroke_linejoin(const html::attr_value& v) const {
        return (*this)("stroke-linejoin", v);
    }
    constexpr auto fill_rule(const html::attr_value& v) const {
        return (*this)("fill-rule", v);
    }
    constexpr auto opacity(const html::attr_value& v) const {
        return (*this)("opacity", v);
    }

    // Dimensions & position
    constexpr auto width(const html::attr_value& v) const {
        return (*this)("width", v);
    }
    constexpr auto height(const html::attr_value& v) const {
        return (*this)("height", v);
    }
    constexpr auto x(const html::attr_value& v) const {
        return (*this)("x", v);
    }
    constexpr auto y(const html::attr_value& v) const {
        return (*this)("y", v);
    }
    constexpr auto rx(const html::attr_value& v) const {
        return (*this)("rx", v);
    }
    constexpr auto ry(const html::attr_value& v) const {
        return (*this)("ry", v);
    }
    constexpr auto cx(const html::attr_value& v) const {
        return (*this)("cx", v);
    }
    constexpr auto cy(const html::attr_value& v) const {
        return (*this)("cy", v);
    }
    constexpr auto r(const html::attr_value& v) const {
        return (*this)("r", v);
    }
    constexpr auto x1(const html::attr_value& v) const {
        return (*this)("x1", v);
    }
    constexpr auto y1(const html::attr_value& v) const {
        return (*this)("y1", v);
    }
    constexpr auto x2(const html::attr_value& v) const {
        return (*this)("x2", v);
    }
    constexpr auto y2(const html::attr_value& v) const {
        return (*this)("y2", v);
    }

    // Path & shape data
    constexpr auto d(const html::attr_value& v) const {
        return (*this)("d", v);
    }
    constexpr auto points(const html::attr_value& v) const {
        return (*this)("points", v);
    }

    // Viewport & transform
    constexpr auto viewBox(const html::attr_value& v) const {
        return (*this)("viewBox", v);
    }
    constexpr auto transform(const html::attr_value& v) const {
        return (*this)("transform", v);
    }
    constexpr auto preserveAspectRatio(const html::attr_value& v) const {
        return (*this)("preserveAspectRatio", v);
    }

    // Gradient
    constexpr auto offset(const html::attr_value& v) const {
        return (*this)("offset", v);
    }
    constexpr auto stop_color(const html::attr_value& v) const {
        return (*this)("stop-color", v);
    }
    constexpr auto stop_opacity(const html::attr_value& v) const {
        return (*this)("stop-opacity", v);
    }
    constexpr auto gradientUnits(const html::attr_value& v) const {
        return (*this)("gradientUnits", v);
    }

    // General
    constexpr auto id(const html::attr_value& v) const {
        return (*this)("id", v);
    }
    constexpr auto class_(const html::attr_value& v) const {
        return (*this)("class", v);
    }
    constexpr auto style(const html::attr_value& v) const {
        return (*this)("style", v);
    }
    constexpr auto href(const html::attr_value& v) const {
        return (*this)("href", v);
    }
    constexpr auto aria_label(const html::attr_value& v) const {
        return (*this)("aria-label", v);
    }
    constexpr auto xmlns(const html::attr_value& v) const {
        return (*this)("xmlns", v);
    }
    constexpr auto vector_effect(const html::attr_value& v) const {
        return (*this)("vector-effect", v);
    }
    constexpr auto stroke_dasharray(const html::attr_value& v) const {
        return (*this)("stroke-dasharray", v);
    }
    constexpr auto stroke_opacity(const html::attr_value& v) const {
        return (*this)("stroke-opacity", v);
    }
    constexpr auto fill_opacity(const html::attr_value& v) const {
        return (*this)("fill-opacity", v);
    }
    constexpr auto text_anchor(const html::attr_value& v) const {
        return (*this)("text-anchor", v);
    }
    constexpr auto role(const html::attr_value& v) const {
        return (*this)("role", v);
    }
    // viewBox="0 0 w h"
    auto viewbox(double w, double h) const {
        return (*this)("viewBox", std::format("0 0 {} {}", html::num(w), html::num(h)));
    }
    auto viewbox(double x, double y, double w, double h) const {
        return (*this)("viewBox", std::format("{} {} {} {}", html::num(x), html::num(y), html::num(w), html::num(h)));
    }
};

inline constexpr attr_t attr;

// ── Path data ─────────────────────────────────────────────────────────────────
// path_data d; d.move(0, h).line(x, y).close(); svg::path{svg::attr.d(d), ...}
// Numbers are rounded to `decimals` places (default 1, enough for pixel coordinates).
class path_data {
public:
    explicit path_data(int decimals = 1) : decimals_(decimals) {}

    auto move(double x, double y) -> path_data& {
        return cmd('M', {x, y});
    }
    auto line(double x, double y) -> path_data& {
        return cmd('L', {x, y});
    }
    auto horizontal(double x) -> path_data& {
        return cmd('H', {x});
    }
    auto vertical(double y) -> path_data& {
        return cmd('V', {y});
    }
    auto curve(double x1, double y1, double x2, double y2, double x, double y) -> path_data& {
        return cmd('C', {x1, y1, x2, y2, x, y});
    }
    auto quad(double x1, double y1, double x, double y) -> path_data& {
        return cmd('Q', {x1, y1, x, y});
    }
    auto arc(double rx, double ry, double rotation, bool large, bool sweep, double x, double y) -> path_data& {
        return cmd('A', {rx, ry, rotation, large ? 1.0 : 0.0, sweep ? 1.0 : 0.0, x, y});
    }
    auto close() -> path_data& {
        d_ += 'Z';
        return *this;
    }
    // Moves to the first point, then lines through the rest. Empty input adds nothing.
    template<std::ranges::range Points>
    auto polyline(const Points& pts) -> path_data& {
        bool first = true;
        for (const auto& [x, y] : pts) {
            first ? move(x, y) : line(x, y);
            first = false;
        }
        return *this;
    }
    [[nodiscard]] auto empty() const -> bool {
        return d_.empty();
    }
    [[nodiscard]] auto str() const -> const std::string& {
        return d_;
    }
    operator html::attr_value() const {
        return html::attr_value{d_};
    }

private:
    auto cmd(char c, std::initializer_list<double> args) -> path_data& {
        d_ += c;
        bool first = true;
        for (double a : args) {
            if (!first) {
                d_ += ',';
            }
            d_ += html::num(a, decimals_);
            first = false;
        }
        return *this;
    }
    std::string d_;
    int decimals_;
};

// "x1,y1 x2,y2 ..." for polyline/polygon points.
template<std::ranges::range Points>
auto points(const Points& pts, int decimals = 1) -> std::string {
    std::string out;
    for (const auto& [x, y] : pts) {
        if (!out.empty()) {
            out += ' ';
        }
        out += html::num(x, decimals);
        out += ',';
        out += html::num(y, decimals);
    }
    return out;
}

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
