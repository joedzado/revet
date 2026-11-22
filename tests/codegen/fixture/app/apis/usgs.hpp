#pragma once
#include <revet/web.hpp>

// Outbound HTTP demo against the USGS Earthquake API.
// Codegen reads this and emits usgs_gov::count(start, end) as an inline
// coroutine in apis.hpp.

namespace usgs_gov {

struct [[revet::model]] CountResponse {
    uint32_t count;
    uint32_t maxAllowed;
};

[[revet::external("GET https://earthquake.usgs.gov/fdsnws/event/1/count?format=geojson")]]
auto count(std::string starttime, std::string endtime) -> web::Task<CountResponse>;

}  // namespace usgs_gov
