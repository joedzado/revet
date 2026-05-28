#pragma once
#include <revet/web.hpp>

inline web::Response not_found(const web::Request&) {
    return { .body = "Not Found", .content_type = "text/plain", .status = web::status::not_found };
}

inline web::Response method_not_allowed(const web::Request&) {
    return { .body = "Method Not Allowed", .content_type = "text/plain", .status = web::status::method_not_allowed };
}

inline web::Response internal_error(const web::Request&, const std::exception& e) {
    return { .body = std::string("Internal Server Error: ") + e.what(),
             .content_type = "text/plain", .status = web::status::internal_error };
}
