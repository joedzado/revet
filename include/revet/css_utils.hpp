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
template<std::string_view html::css_props::* Field>
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
inline constexpr type _0{sp::_0};
inline constexpr type _px{sp::_px};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _5{sp::_5};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
inline constexpr type _10{sp::_10};
inline constexpr type _12{sp::_12};
inline constexpr type _16{sp::_16};
} // namespace gap
namespace row_gap {
using type = mod<&html::css_props::row_gap>;
inline constexpr type _0{sp::_0};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
} // namespace row_gap
namespace col_gap {
using type = mod<&html::css_props::column_gap>;
inline constexpr type _0{sp::_0};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
} // namespace col_gap

// ── Padding ───────────────────────────────────────────────────────────────────
namespace p {
using type = mod<&html::css_props::padding>;
inline constexpr type _0{sp::_0};
inline constexpr type _px{sp::_px};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _5{sp::_5};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
inline constexpr type _10{sp::_10};
inline constexpr type _12{sp::_12};
inline constexpr type _16{sp::_16};
} // namespace p
namespace pt {
using type = mod<&html::css_props::padding_top>;
inline constexpr type _0{sp::_0};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
inline constexpr type _12{sp::_12};
} // namespace pt
namespace pb {
using type = mod<&html::css_props::padding_bottom>;
inline constexpr type _0{sp::_0};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
inline constexpr type _12{sp::_12};
} // namespace pb
namespace pl {
using type = mod<&html::css_props::padding_left>;
inline constexpr type _0{sp::_0};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
} // namespace pl
namespace pr {
using type = mod<&html::css_props::padding_right>;
inline constexpr type _0{sp::_0};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
} // namespace pr

// ── Margin ────────────────────────────────────────────────────────────────────
namespace m {
using type = mod<&html::css_props::margin>;
inline constexpr type _0{sp::_0};
inline constexpr type _auto{sp::_auto};
inline constexpr type _px{sp::_px};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
inline constexpr type _12{sp::_12};
inline constexpr type _16{sp::_16};
} // namespace m
namespace mt {
using type = mod<&html::css_props::margin_top>;
inline constexpr type _0{sp::_0};
inline constexpr type _auto{sp::_auto};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
} // namespace mt
namespace mb {
using type = mod<&html::css_props::margin_bottom>;
inline constexpr type _0{sp::_0};
inline constexpr type _auto{sp::_auto};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _3{sp::_3};
inline constexpr type _4{sp::_4};
inline constexpr type _6{sp::_6};
inline constexpr type _8{sp::_8};
} // namespace mb
namespace ml {
using type = mod<&html::css_props::margin_left>;
inline constexpr type _0{sp::_0};
inline constexpr type _auto{sp::_auto};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _4{sp::_4};
inline constexpr type _8{sp::_8};
} // namespace ml
namespace mr {
using type = mod<&html::css_props::margin_right>;
inline constexpr type _0{sp::_0};
inline constexpr type _auto{sp::_auto};
inline constexpr type _1{sp::_1};
inline constexpr type _2{sp::_2};
inline constexpr type _4{sp::_4};
inline constexpr type _8{sp::_8};
} // namespace mr

// ── Flex alignment ────────────────────────────────────────────────────────────
namespace items {
using type = mod<&html::css_props::align_items>;
inline constexpr type start{"flex-start"};
inline constexpr type end{"flex-end"};
inline constexpr type center{"center"};
inline constexpr type stretch{"stretch"};
inline constexpr type baseline{"baseline"};
} // namespace items
namespace justify {
using type = mod<&html::css_props::justify_content>;
inline constexpr type start{"flex-start"};
inline constexpr type end{"flex-end"};
inline constexpr type center{"center"};
inline constexpr type between{"space-between"};
inline constexpr type around{"space-around"};
inline constexpr type evenly{"space-evenly"};
} // namespace justify
namespace self {
using type = mod<&html::css_props::align_self>;
inline constexpr type start{"flex-start"};
inline constexpr type end{"flex-end"};
inline constexpr type center{"center"};
inline constexpr type stretch{"stretch"};
inline constexpr type auto_{"auto"};
} // namespace self
namespace direction {
using type = mod<&html::css_props::flex_direction>;
inline constexpr type row{"row"};
inline constexpr type col{"column"};
inline constexpr type row_rev{"row-reverse"};
inline constexpr type col_rev{"column-reverse"};
} // namespace direction
namespace wrap {
using type = mod<&html::css_props::flex_wrap>;
inline constexpr type wrap{"wrap"};
inline constexpr type nowrap{"nowrap"};
inline constexpr type wrap_rev{"wrap-reverse"};
} // namespace wrap
namespace grow {
using type = mod<&html::css_props::flex_grow>;
inline constexpr type _0{"0"};
inline constexpr type _1{"1"};
} // namespace grow
namespace shrink {
using type = mod<&html::css_props::flex_shrink>;
inline constexpr type _0{"0"};
inline constexpr type _1{"1"};
} // namespace shrink

// ── Grid ──────────────────────────────────────────────────────────────────────
namespace cols {
using type = mod<&html::css_props::grid_template_columns>;
inline constexpr type _1{"repeat(1, 1fr)"};
inline constexpr type _2{"repeat(2, 1fr)"};
inline constexpr type _3{"repeat(3, 1fr)"};
inline constexpr type _4{"repeat(4, 1fr)"};
inline constexpr type _5{"repeat(5, 1fr)"};
inline constexpr type _6{"repeat(6, 1fr)"};
inline constexpr type _12{"repeat(12, 1fr)"};
inline constexpr type auto_{"auto"};
} // namespace cols
namespace rows {
using type = mod<&html::css_props::grid_template_rows>;
inline constexpr type _1{"repeat(1, 1fr)"};
inline constexpr type _2{"repeat(2, 1fr)"};
inline constexpr type _3{"repeat(3, 1fr)"};
inline constexpr type _4{"repeat(4, 1fr)"};
inline constexpr type auto_{"auto"};
} // namespace rows

// ── Sizing ────────────────────────────────────────────────────────────────────
namespace w {
using type = mod<&html::css_props::width>;
inline constexpr type full{"100%"};
inline constexpr type auto_{"auto"};
inline constexpr type screen{"100vw"};
inline constexpr type fit{"fit-content"};
inline constexpr type min_{"min-content"};
inline constexpr type max_{"max-content"};
} // namespace w
namespace h {
using type = mod<&html::css_props::height>;
inline constexpr type full{"100%"};
inline constexpr type auto_{"auto"};
inline constexpr type screen{"100vh"};
inline constexpr type fit{"fit-content"};
inline constexpr type min_{"min-content"};
} // namespace h
namespace min_w {
using type = mod<&html::css_props::min_width>;
inline constexpr type _0{"0"};
inline constexpr type full{"100%"};
inline constexpr type min_{"min-content"};
} // namespace min_w
namespace max_w {
using type = mod<&html::css_props::max_width>;
inline constexpr type full{"100%"};
inline constexpr type none{"none"};
inline constexpr type sm{"24rem"};
inline constexpr type md{"28rem"};
inline constexpr type lg{"32rem"};
inline constexpr type xl{"36rem"};
inline constexpr type _2xl{"42rem"};
inline constexpr type _3xl{"48rem"};
inline constexpr type _4xl{"56rem"};
inline constexpr type _5xl{"64rem"};
inline constexpr type _6xl{"72rem"};
inline constexpr type _7xl{"80rem"};
} // namespace max_w

// ── Typography ────────────────────────────────────────────────────────────────
namespace font {
using type = mod<&html::css_props::font_size>;
inline constexpr type xs{"0.75rem"};
inline constexpr type sm{"0.875rem"};
inline constexpr type base{"1rem"};
inline constexpr type lg{"1.125rem"};
inline constexpr type xl{"1.25rem"};
inline constexpr type _2xl{"1.5rem"};
inline constexpr type _3xl{"1.875rem"};
inline constexpr type _4xl{"2.25rem"};
inline constexpr type _5xl{"3rem"};
inline constexpr type _6xl{"3.75rem"};
} // namespace font
namespace weight {
using type = mod<&html::css_props::font_weight>;
inline constexpr type thin{"100"};
inline constexpr type light{"300"};
inline constexpr type normal{"400"};
inline constexpr type medium{"500"};
inline constexpr type semibold{"600"};
inline constexpr type bold{"700"};
inline constexpr type extrabold{"800"};
} // namespace weight
namespace leading {
using type = mod<&html::css_props::line_height>;
inline constexpr type none{"1"};
inline constexpr type tight{"1.25"};
inline constexpr type snug{"1.375"};
inline constexpr type normal{"1.5"};
inline constexpr type relaxed{"1.625"};
inline constexpr type loose{"2"};
} // namespace leading
namespace text_align {
using type = mod<&html::css_props::text_align>;
inline constexpr type left{"left"};
inline constexpr type center{"center"};
inline constexpr type right{"right"};
inline constexpr type justify{"justify"};
} // namespace text_align

// ── Border ────────────────────────────────────────────────────────────────────
namespace radius {
using type = mod<&html::css_props::border_radius>;
inline constexpr type none{"0"};
inline constexpr type sm{"0.125rem"};
inline constexpr type base{"0.25rem"};
inline constexpr type md{"0.375rem"};
inline constexpr type lg{"0.5rem"};
inline constexpr type xl{"0.75rem"};
inline constexpr type _2xl{"1rem"};
inline constexpr type _3xl{"1.5rem"};
inline constexpr type full{"9999px"};
} // namespace radius

// ── Position / overflow / cursor ─────────────────────────────────────────────
namespace pos {
using type = mod<&html::css_props::position>;
inline constexpr type static_{"static"};
inline constexpr type relative{"relative"};
inline constexpr type absolute{"absolute"};
inline constexpr type fixed{"fixed"};
inline constexpr type sticky{"sticky"};
} // namespace pos
namespace overflow {
using type = mod<&html::css_props::overflow>;
inline constexpr type auto_{"auto"};
inline constexpr type hidden{"hidden"};
inline constexpr type visible{"visible"};
inline constexpr type scroll{"scroll"};
} // namespace overflow
namespace overflow_x {
using type = mod<&html::css_props::overflow_x>;
inline constexpr type auto_{"auto"};
inline constexpr type hidden{"hidden"};
inline constexpr type visible{"visible"};
inline constexpr type scroll{"scroll"};
} // namespace overflow_x
namespace overflow_y {
using type = mod<&html::css_props::overflow_y>;
inline constexpr type auto_{"auto"};
inline constexpr type hidden{"hidden"};
inline constexpr type visible{"visible"};
inline constexpr type scroll{"scroll"};
} // namespace overflow_y
namespace cursor {
using type = mod<&html::css_props::cursor>;
inline constexpr type pointer{"pointer"};
inline constexpr type default_{"default"};
inline constexpr type not_allowed{"not-allowed"};
inline constexpr type grab{"grab"};
inline constexpr type text_{"text"};
inline constexpr type none{"none"};
} // namespace cursor
namespace opacity {
using type = mod<&html::css_props::opacity>;
inline constexpr type _0{"0"};
inline constexpr type _25{"0.25"};
inline constexpr type _50{"0.5"};
inline constexpr type _75{"0.75"};
inline constexpr type _100{"1"};
} // namespace opacity
namespace z {
using type = mod<&html::css_props::z_index>;
inline constexpr type _0{"0"};
inline constexpr type _10{"10"};
inline constexpr type _20{"20"};
inline constexpr type _30{"30"};
inline constexpr type _40{"40"};
inline constexpr type _50{"50"};
inline constexpr type auto_{"auto"};
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
    return html::css_class(p);
}

template<typename... Mods>
inline auto grid(Mods... mods) -> html::css_class {
    html::css_props p{.display = "grid"};
    (mods(p), ...);
    return html::css_class(p);
}

// No display preset — apply mods to a blank css_props.
template<typename... Mods>
inline auto make(Mods... mods) -> html::css_class {
    html::css_props p{};
    (mods(p), ...);
    return html::css_class(p);
}

} // namespace css
