#pragma once
#include <revet/web.hpp>

// Outbound HTTP example — USGS Earthquake Hazards Program.
// The [[revet::external]] attribute drives codegen: gen_api.py reads this
// header and emits an inline coroutine definition in apis.hpp.
//
// The response struct is [[revet::model]] so JSON deserialization is free.

namespace usgs_gov {

struct [[revet::model]] CountResponse {
    uint32_t count;
    uint32_t maxAllowed;
};

[[revet::external("GET https://earthquake.usgs.gov/fdsnws/event/1/count?format=geojson")]]
auto count(std::string starttime, std::string endtime) -> web::Task<CountResponse>;

}  // namespace usgs_gov
