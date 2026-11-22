// Auto-generated — do not edit
#pragma once
#include <coroutine>
#include <revet/task.hpp>
#include <revet/http.hpp>
#include <revet/json.hpp>
#include "models.hpp"
#include <string>

#include "../../app/apis/usgs.hpp"

namespace usgs_gov {
inline auto count(std::string starttime, std::string endtime) -> web::Task<CountResponse> {
    auto url  = std::string("https://earthquake.usgs.gov/fdsnws/event/1/count?format=geojson") + "&starttime=" + std::string(starttime) + "&endtime=" + std::string(endtime);
    auto resp = co_await http::get(url);
    if (!resp.ok()) throw http::Error(resp.status_code, resp.body);
    revet::json::Document doc(resp.body);
    co_return revet::json::from_json<CountResponse>(doc);
}

} // namespace usgs_gov
