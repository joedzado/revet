// Auto-generated — do not edit
#pragma once
#include <revet/web.hpp>

namespace web::toggles {

inline bool compact(const web::ParamMap& cookies) {
    auto v = cookies.get("_cw_compact");
    return v.empty() ? false : (v == "1");
}

inline bool sb_open(const web::ParamMap& cookies) {
    auto v = cookies.get("_cw_sb_open");
    return v.empty() ? true : (v == "1");
}

inline bool dark_mode(const web::ParamMap& cookies) {
    auto v = cookies.get("_cw_dark_mode");
    return v.empty() ? false : (v == "1");
}

} // namespace web::toggles
