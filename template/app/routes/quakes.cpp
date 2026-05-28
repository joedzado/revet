#include <revet/web.hpp>
#include <revet/log.hpp>

static const log::TuTag LOG(log::tags::ROUTES);

// GET /quakes?start=YYYY-MM-DD&end=YYYY-MM-DD
// Outbound HTTP example. The usgs_gov::count() function is generated
// from app/apis/usgs.hpp by gen_api.py.
static auto render_get(const web::Request& req) -> web::Task<web::Response> {
    auto start = req.query.get("start");
    auto end   = req.query.get("end");
    if (start.empty()) start = "2024-01-01";
    if (end.empty())   end   = "2024-01-02";

    const auto data = co_await usgs_gov::count(start, end);

    const auto content = html::div{styles::container, styles::page_content,
        html::h1{styles::page_title, "USGS Earthquakes"},
        html::div{styles::detail_card,
            html::div{styles::detail_row,
                html::span{styles::detail_label, "Window"},
                html::span{styles::detail_value, start + " → " + end}
            },
            html::div{styles::detail_row,
                html::span{styles::detail_label, "Count"},
                html::span{html::id{"quake-count"}, styles::detail_value, data.count}
            },
            html::div{styles::detail_row,
                html::span{styles::detail_label, "Max allowed"},
                html::span{styles::detail_value, data.maxAllowed}
            }
        }
    };

    co_return layout_main(req, "Earthquakes — {{name}}", content);
}
