#include <revet/web.hpp>

static auto render_get(const web::Request& req) -> web::Response {
    auto file = std::string(req.params.get("file"));
    if (file.find("..") != std::string::npos || !file.ends_with(".woff2"))
        return {.body = "Not Found", .status = web::status::not_found};
    return web::serve_file("fonts/" + file);
}
