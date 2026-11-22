#pragma once
#include <revet/svg.hpp>

static constexpr auto ico_home() -> html::Node {
    return svg::svg{

        svg::attr.viewBox("0 0 24 24"), svg::attr.fill("none"),
        svg::attr.stroke("currentColor"), svg::attr.stroke_width("2"),
        svg::path{svg::attr.d("M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z")},
        svg::polyline{svg::attr.points("9 22 9 12 15 12 15 22")},
    };
}
static constexpr auto ico_users() -> html::Node {
    return svg::svg{

        svg::attr.viewBox("0 0 24 24"), svg::attr.fill("none"),
        svg::attr.stroke("currentColor"), svg::attr.stroke_width("2"),
        svg::path{svg::attr.d("M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2")},
        svg::circle{svg::attr.cx("9"), svg::attr.cy("7"), svg::attr.r("4")},
        svg::path{svg::attr.d("M23 21v-2a4 4 0 0 0-3-3.87")},
        svg::path{svg::attr.d("M16 3.13a4 4 0 0 1 0 7.75")},
    };
}
static constexpr auto ico_contact() -> html::Node {
    return svg::svg{

        svg::attr.viewBox("0 0 24 24"), svg::attr.fill("none"),
        svg::attr.stroke("currentColor"), svg::attr.stroke_width("2"),
        svg::path{svg::attr.d("M4 4h16c1.1 0 2 .9 2 2v12c0 1.1-.9 2-2 2H4c-1.1 0-2-.9-2-2V6c0-1.1.9-2 2-2z")},
        svg::polyline{svg::attr.points("22,6 12,13 2,6")},
    };
}
static constexpr auto ico_about() -> html::Node {
    return svg::svg{

        svg::attr.viewBox("0 0 24 24"), svg::attr.fill("none"),
        svg::attr.stroke("currentColor"), svg::attr.stroke_width("2"),
        svg::circle{svg::attr.cx("12"), svg::attr.cy("12"), svg::attr.r("10")},
        svg::line{svg::attr.x1("12"), svg::attr.y1("8"), svg::attr.x2("12"), svg::attr.y2("12")},
        svg::line{svg::attr.x1("12"), svg::attr.y1("16"), svg::attr.x2("12.01"), svg::attr.y2("16")},
    };
}
static constexpr auto ico_live() -> html::Node {
    return svg::svg{

        svg::attr.viewBox("0 0 24 24"), svg::attr.fill("none"),
        svg::attr.stroke("currentColor"), svg::attr.stroke_width("2"),
        svg::circle{svg::attr.cx("12"), svg::attr.cy("12"), svg::attr.r("2")},
        svg::path{svg::attr.d("M16.24 7.76a6 6 0 0 1 0 8.49")},
        svg::path{svg::attr.d("M7.76 16.24a6 6 0 0 1 0-8.49")},
        svg::path{svg::attr.d("M20.07 3.93a10 10 0 0 1 0 14.14")},
        svg::path{svg::attr.d("M3.93 20.07a10 10 0 0 1 0-14.14")},
    };
}
static constexpr auto ico_components() -> html::Node {
    return svg::svg{

        svg::attr.viewBox("0 0 24 24"), svg::attr.fill("none"),
        svg::attr.stroke("currentColor"), svg::attr.stroke_width("2"),
        svg::rect{svg::attr.x("3"),  svg::attr.y("3"),  svg::attr.width("7"), svg::attr.height("7")},
        svg::rect{svg::attr.x("14"), svg::attr.y("3"),  svg::attr.width("7"), svg::attr.height("7")},
        svg::rect{svg::attr.x("14"), svg::attr.y("14"), svg::attr.width("7"), svg::attr.height("7")},
        svg::rect{svg::attr.x("3"),  svg::attr.y("14"), svg::attr.width("7"), svg::attr.height("7")},
    };
}
static constexpr auto ico_upload() -> html::Node {
    return svg::svg{

        svg::attr.viewBox("0 0 24 24"), svg::attr.fill("none"),
        svg::attr.stroke("currentColor"), svg::attr.stroke_width("2"),
        svg::polyline{svg::attr.points("16 16 12 12 8 16")},
        svg::line{svg::attr.x1("12"), svg::attr.y1("12"), svg::attr.x2("12"), svg::attr.y2("21")},
        svg::path{svg::attr.d("M20.39 18.39A5 5 0 0 0 18 9h-1.26A8 8 0 1 0 3 16.3")},
    };
}
static constexpr auto ico_interactive() -> html::Node {
    return svg::svg{

        svg::attr.viewBox("0 0 24 24"), svg::attr.fill("none"),
        svg::attr.stroke("currentColor"), svg::attr.stroke_width("2"),
        svg::polygon{svg::attr.points("13 2 3 14 12 14 11 22 21 10 12 10 13 2")},
    };
}
static constexpr auto ico_chat() -> html::Node {
    return svg::svg{
        svg::attr.viewBox("0 0 24 24"), svg::attr.fill("none"),
        svg::attr.stroke("currentColor"), svg::attr.stroke_width("2"),
        svg::path{svg::attr.d("M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z")},
    };
}
static constexpr auto ico_team() -> html::Node {
    return svg::svg{

        svg::attr.viewBox("0 0 24 24"), svg::attr.fill("none"),
        svg::attr.stroke("currentColor"), svg::attr.stroke_width("2"),
        svg::path{svg::attr.d("M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2")},
        svg::circle{svg::attr.cx("9"), svg::attr.cy("7"), svg::attr.r("4")},
        svg::line{svg::attr.x1("23"), svg::attr.y1("11"), svg::attr.x2("17"), svg::attr.y2("11")},
        svg::line{svg::attr.x1("20"), svg::attr.y1("8"), svg::attr.x2("20"), svg::attr.y2("14")},
    };
}
