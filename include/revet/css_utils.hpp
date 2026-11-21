#pragma once
#include "css.hpp"

//
// Scale constants convert implicitly to string_view — use directly in css_props:
//   html::css_class({.gap = css::sp::_4, .font_size = css::font::sm})
//
// Or pass as modifiers to builder functions:
//   static const auto card = css::flex(css::gap::_4, css::items::center);
//   static const auto hero = css::grid(css::cols::_3, css::gap::_6);

namespace css {

// Internal: a modifier that sets one css_props field.
// Implicitly converts to string_view so it also works as a raw value.
template<std::string_view html::css_props::*Field>
struct mod {
    std::string_view v;
    constexpr void operator()(html::css_props& p) const {
        p.*Field = v;
    }
    constexpr operator std::string_view() const {
        return v;
    }
};

// ── Spacing scale ─────────────────────────────────────────────────────────────
// Raw string_view values shared across gap, padding, margin modifiers.
namespace sp {
inline constexpr std::string_view _0 = "0";
inline constexpr std::string_view _px = "1px";
inline constexpr std::string_view _0_5 = "0.125rem";
inline constexpr std::string_view _1 = "0.25rem";
inline constexpr std::string_view _1_5 = "0.375rem";
inline constexpr std::string_view _2 = "0.5rem";
inline constexpr std::string_view _2_5 = "0.625rem";
inline constexpr std::string_view _3 = "0.75rem";
inline constexpr std::string_view _3_5 = "0.875rem";
inline constexpr std::string_view _4 = "1rem";
inline constexpr std::string_view _5 = "1.25rem";
inline constexpr std::string_view _6 = "1.5rem";
inline constexpr std::string_view _7 = "1.75rem";
inline constexpr std::string_view _8 = "2rem";
inline constexpr std::string_view _10 = "2.5rem";
inline constexpr std::string_view _12 = "3rem";
inline constexpr std::string_view _16 = "4rem";
inline constexpr std::string_view _20 = "5rem";
inline constexpr std::string_view _24 = "6rem";
inline constexpr std::string_view _32 = "8rem";
inline constexpr std::string_view _auto = "auto";
inline constexpr std::string_view _full = "100%";
} // namespace sp

// ── Gap ───────────────────────────────────────────────────────────────────────
namespace gap {
using type = mod<&html::css_props::gap>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _px{.v = sp::_px};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _5{.v = sp::_5};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
inline constexpr type _10{.v = sp::_10};
inline constexpr type _12{.v = sp::_12};
inline constexpr type _16{.v = sp::_16};
} // namespace gap
namespace row_gap {
using type = mod<&html::css_props::row_gap>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
} // namespace row_gap
namespace col_gap {
using type = mod<&html::css_props::column_gap>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
} // namespace col_gap

// ── Padding ───────────────────────────────────────────────────────────────────
namespace p {
using type = mod<&html::css_props::padding>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _px{.v = sp::_px};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _5{.v = sp::_5};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
inline constexpr type _10{.v = sp::_10};
inline constexpr type _12{.v = sp::_12};
inline constexpr type _16{.v = sp::_16};
} // namespace p
namespace pt {
using type = mod<&html::css_props::padding_top>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
inline constexpr type _12{.v = sp::_12};
} // namespace pt
namespace pb {
using type = mod<&html::css_props::padding_bottom>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
inline constexpr type _12{.v = sp::_12};
} // namespace pb
namespace pl {
using type = mod<&html::css_props::padding_left>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
} // namespace pl
namespace pr {
using type = mod<&html::css_props::padding_right>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
} // namespace pr

// ── Margin ────────────────────────────────────────────────────────────────────
namespace m {
using type = mod<&html::css_props::margin>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _auto{.v = sp::_auto};
inline constexpr type _px{.v = sp::_px};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
inline constexpr type _12{.v = sp::_12};
inline constexpr type _16{.v = sp::_16};
} // namespace m
namespace mt {
using type = mod<&html::css_props::margin_top>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _auto{.v = sp::_auto};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
} // namespace mt
namespace mb {
using type = mod<&html::css_props::margin_bottom>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _auto{.v = sp::_auto};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _3{.v = sp::_3};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _6{.v = sp::_6};
inline constexpr type _8{.v = sp::_8};
} // namespace mb
namespace ml {
using type = mod<&html::css_props::margin_left>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _auto{.v = sp::_auto};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _8{.v = sp::_8};
} // namespace ml
namespace mr {
using type = mod<&html::css_props::margin_right>;
inline constexpr type _0{.v = sp::_0};
inline constexpr type _auto{.v = sp::_auto};
inline constexpr type _1{.v = sp::_1};
inline constexpr type _2{.v = sp::_2};
inline constexpr type _4{.v = sp::_4};
inline constexpr type _8{.v = sp::_8};
} // namespace mr

// ── Flex alignment ────────────────────────────────────────────────────────────
namespace items {
using type = mod<&html::css_props::align_items>;
inline constexpr type start{.v = "flex-start"};
inline constexpr type end{.v = "flex-end"};
inline constexpr type center{.v = "center"};
inline constexpr type stretch{.v = "stretch"};
inline constexpr type baseline{.v = "baseline"};
} // namespace items
namespace justify {
using type = mod<&html::css_props::justify_content>;
inline constexpr type start{.v = "flex-start"};
inline constexpr type end{.v = "flex-end"};
inline constexpr type center{.v = "center"};
inline constexpr type between{.v = "space-between"};
inline constexpr type around{.v = "space-around"};
inline constexpr type evenly{.v = "space-evenly"};
} // namespace justify
namespace self {
using type = mod<&html::css_props::align_self>;
inline constexpr type start{.v = "flex-start"};
inline constexpr type end{.v = "flex-end"};
inline constexpr type center{.v = "center"};
inline constexpr type stretch{.v = "stretch"};
inline constexpr type auto_{.v = "auto"};
} // namespace self
namespace direction {
using type = mod<&html::css_props::flex_direction>;
inline constexpr type row{.v = "row"};
inline constexpr type col{.v = "column"};
inline constexpr type row_rev{.v = "row-reverse"};
inline constexpr type col_rev{.v = "column-reverse"};
} // namespace direction
namespace wrap {
using type = mod<&html::css_props::flex_wrap>;
inline constexpr type wrap{.v = "wrap"};
inline constexpr type nowrap{.v = "nowrap"};
inline constexpr type wrap_rev{.v = "wrap-reverse"};
} // namespace wrap
namespace grow {
using type = mod<&html::css_props::flex_grow>;
inline constexpr type _0{.v = "0"};
inline constexpr type _1{.v = "1"};
} // namespace grow
namespace shrink {
using type = mod<&html::css_props::flex_shrink>;
inline constexpr type _0{.v = "0"};
inline constexpr type _1{.v = "1"};
} // namespace shrink

// ── Grid ──────────────────────────────────────────────────────────────────────
namespace cols {
using type = mod<&html::css_props::grid_template_columns>;
inline constexpr type _1{.v = "repeat(1, 1fr)"};
inline constexpr type _2{.v = "repeat(2, 1fr)"};
inline constexpr type _3{.v = "repeat(3, 1fr)"};
inline constexpr type _4{.v = "repeat(4, 1fr)"};
inline constexpr type _5{.v = "repeat(5, 1fr)"};
inline constexpr type _6{.v = "repeat(6, 1fr)"};
inline constexpr type _12{.v = "repeat(12, 1fr)"};
inline constexpr type auto_{.v = "auto"};
} // namespace cols
namespace rows {
using type = mod<&html::css_props::grid_template_rows>;
inline constexpr type _1{.v = "repeat(1, 1fr)"};
inline constexpr type _2{.v = "repeat(2, 1fr)"};
inline constexpr type _3{.v = "repeat(3, 1fr)"};
inline constexpr type _4{.v = "repeat(4, 1fr)"};
inline constexpr type auto_{.v = "auto"};
} // namespace rows

// ── Sizing ────────────────────────────────────────────────────────────────────
namespace w {
using type = mod<&html::css_props::width>;
inline constexpr type full{.v = "100%"};
inline constexpr type auto_{.v = "auto"};
inline constexpr type screen{.v = "100vw"};
inline constexpr type fit{.v = "fit-content"};
inline constexpr type min_{.v = "min-content"};
inline constexpr type max_{.v = "max-content"};
} // namespace w
namespace h {
using type = mod<&html::css_props::height>;
inline constexpr type full{.v = "100%"};
inline constexpr type auto_{.v = "auto"};
inline constexpr type screen{.v = "100vh"};
inline constexpr type fit{.v = "fit-content"};
inline constexpr type min_{.v = "min-content"};
} // namespace h
namespace min_w {
using type = mod<&html::css_props::min_width>;
inline constexpr type _0{.v = "0"};
inline constexpr type full{.v = "100%"};
inline constexpr type min_{.v = "min-content"};
} // namespace min_w
namespace max_w {
using type = mod<&html::css_props::max_width>;
inline constexpr type full{.v = "100%"};
inline constexpr type none{.v = "none"};
inline constexpr type sm{.v = "24rem"};
inline constexpr type md{.v = "28rem"};
inline constexpr type lg{.v = "32rem"};
inline constexpr type xl{.v = "36rem"};
inline constexpr type _2xl{.v = "42rem"};
inline constexpr type _3xl{.v = "48rem"};
inline constexpr type _4xl{.v = "56rem"};
inline constexpr type _5xl{.v = "64rem"};
inline constexpr type _6xl{.v = "72rem"};
inline constexpr type _7xl{.v = "80rem"};
} // namespace max_w

// ── Typography ────────────────────────────────────────────────────────────────
namespace font {
using type = mod<&html::css_props::font_size>;
inline constexpr type xs{.v = "0.75rem"};
inline constexpr type sm{.v = "0.875rem"};
inline constexpr type base{.v = "1rem"};
inline constexpr type lg{.v = "1.125rem"};
inline constexpr type xl{.v = "1.25rem"};
inline constexpr type _2xl{.v = "1.5rem"};
inline constexpr type _3xl{.v = "1.875rem"};
inline constexpr type _4xl{.v = "2.25rem"};
inline constexpr type _5xl{.v = "3rem"};
inline constexpr type _6xl{.v = "3.75rem"};
} // namespace font
namespace weight {
using type = mod<&html::css_props::font_weight>;
inline constexpr type thin{.v = "100"};
inline constexpr type light{.v = "300"};
inline constexpr type normal{.v = "400"};
inline constexpr type medium{.v = "500"};
inline constexpr type semibold{.v = "600"};
inline constexpr type bold{.v = "700"};
inline constexpr type extrabold{.v = "800"};
} // namespace weight
namespace leading {
using type = mod<&html::css_props::line_height>;
inline constexpr type none{.v = "1"};
inline constexpr type tight{.v = "1.25"};
inline constexpr type snug{.v = "1.375"};
inline constexpr type normal{.v = "1.5"};
inline constexpr type relaxed{.v = "1.625"};
inline constexpr type loose{.v = "2"};
} // namespace leading
namespace text_align {
using type = mod<&html::css_props::text_align>;
inline constexpr type left{.v = "left"};
inline constexpr type center{.v = "center"};
inline constexpr type right{.v = "right"};
inline constexpr type justify{.v = "justify"};
} // namespace text_align

// ── Border ────────────────────────────────────────────────────────────────────
namespace radius {
using type = mod<&html::css_props::border_radius>;
inline constexpr type none{.v = "0"};
inline constexpr type sm{.v = "0.125rem"};
inline constexpr type base{.v = "0.25rem"};
inline constexpr type md{.v = "0.375rem"};
inline constexpr type lg{.v = "0.5rem"};
inline constexpr type xl{.v = "0.75rem"};
inline constexpr type _2xl{.v = "1rem"};
inline constexpr type _3xl{.v = "1.5rem"};
inline constexpr type full{.v = "9999px"};
} // namespace radius

// ── Position / overflow / cursor ─────────────────────────────────────────────
namespace pos {
using type = mod<&html::css_props::position>;
inline constexpr type static_{.v = "static"};
inline constexpr type relative{.v = "relative"};
inline constexpr type absolute{.v = "absolute"};
inline constexpr type fixed{.v = "fixed"};
inline constexpr type sticky{.v = "sticky"};
} // namespace pos
namespace overflow {
using type = mod<&html::css_props::overflow>;
inline constexpr type auto_{.v = "auto"};
inline constexpr type hidden{.v = "hidden"};
inline constexpr type visible{.v = "visible"};
inline constexpr type scroll{.v = "scroll"};
} // namespace overflow
namespace overflow_x {
using type = mod<&html::css_props::overflow_x>;
inline constexpr type auto_{.v = "auto"};
inline constexpr type hidden{.v = "hidden"};
inline constexpr type visible{.v = "visible"};
inline constexpr type scroll{.v = "scroll"};
} // namespace overflow_x
namespace overflow_y {
using type = mod<&html::css_props::overflow_y>;
inline constexpr type auto_{.v = "auto"};
inline constexpr type hidden{.v = "hidden"};
inline constexpr type visible{.v = "visible"};
inline constexpr type scroll{.v = "scroll"};
} // namespace overflow_y
namespace cursor {
using type = mod<&html::css_props::cursor>;
inline constexpr type pointer{.v = "pointer"};
inline constexpr type default_{.v = "default"};
inline constexpr type not_allowed{.v = "not-allowed"};
inline constexpr type grab{.v = "grab"};
inline constexpr type text_{.v = "text"};
inline constexpr type none{.v = "none"};
} // namespace cursor
namespace opacity {
using type = mod<&html::css_props::opacity>;
inline constexpr type _0{.v = "0"};
inline constexpr type _25{.v = "0.25"};
inline constexpr type _50{.v = "0.5"};
inline constexpr type _75{.v = "0.75"};
inline constexpr type _100{.v = "1"};
} // namespace opacity
namespace z {
using type = mod<&html::css_props::z_index>;
inline constexpr type _0{.v = "0"};
inline constexpr type _10{.v = "10"};
inline constexpr type _20{.v = "20"};
inline constexpr type _30{.v = "30"};
inline constexpr type _40{.v = "40"};
inline constexpr type _50{.v = "50"};
inline constexpr type auto_{.v = "auto"};
} // namespace z

// ── Builder functions ─────────────────────────────────────────────────────────
// Apply modifiers to a css_props preset and return a scoped css_class.
// Cache with `static const auto` in component scope.
//
//   static const auto row = css::flex(css::gap::_4, css::items::center);
//   static const auto hero = css::grid(css::cols::_3, css::gap::_6);

template<typename... Mods>
inline auto flex(Mods... mods) -> html::css_class {
    html::css_props p{.display = "flex"};
    (mods(p), ...);
    return {p};
}

template<typename... Mods>
inline auto grid(Mods... mods) -> html::css_class {
    html::css_props p{.display = "grid"};
    (mods(p), ...);
    return {p};
}

// No display preset — apply mods to a blank css_props.
template<typename... Mods>
inline auto make(Mods... mods) -> html::css_class {
    html::css_props p{};
    (mods(p), ...);
    return {p};
}

} // namespace css
