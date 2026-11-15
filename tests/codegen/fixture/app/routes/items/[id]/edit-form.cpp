#include "styles.hpp"
#include <string>
static auto render_get(const web::Request& req, web::State<Cart> cart,
                       web::Flash<Notice> notice) -> web::Response {
    return {};
}
web::Task<web::Response> render_post(const web::Request& req, web::Flash<Notice> notice) {
    auto ctx2 = web::Context::current();
    ctx2.flash(Notice{"saved", false});
    ctx2.has<Cart>();
    co_return web::Response{};
}
static void render_sse(const web::Request& req, web::Flash<Notice> n, web::SSEWriter& sse) {}
