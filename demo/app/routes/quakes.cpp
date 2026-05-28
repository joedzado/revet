#include <revet/web.hpp>
#include <revet/log.hpp>

static const log::TuTag LOG(log::tags::ROUTES);

// GET /quakes?start=YYYY-MM-DD&end=YYYY-MM-DD
// Outbound HTTP demo against the USGS Earthquake API.
// usgs_gov::count() is generated from demo/app/apis/usgs.hpp.
static auto render_get(const web::Request& req) -> web::Task<web::Response> {
    auto start = req.query.get("start");
    auto end   = req.query.get("end");
    if (start.empty()) start = "2024-01-01";
    if (end.empty())   end   = "2024-01-02";

    const auto data = co_await usgs_gov::count(start, end);

    const auto content = html::div{
        html::h1{"USGS Earthquakes"},
        html::p{html::strong{"Window: "}, start, " → ", end},
        html::p{html::strong{"Count: "}, html::span{html::id{"quake-count"}, data.count}},
        html::p{html::strong{"Max allowed: "}, data.maxAllowed}
    };

    co_return layout_page(req, "Quakes", "/quakes", content);
}
