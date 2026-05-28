#pragma once
#include <string>
#include <string_view>

namespace web {
struct ParamMap;
}

namespace web::states {
template<typename T>
struct Traits;
template<typename T>
std::string pack(const T& val);

static constexpr std::string_view cookie_prefix = "_cw_";

// Check whether a state cookie is present in a request's cookie map.
// Use in route_guards() where web::State<T> injection hasn't happened yet.
template<typename T>
[[nodiscard]] inline bool has(const web::ParamMap& cookies);
} // namespace web::states
