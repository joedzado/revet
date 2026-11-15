#include <revet/websocket.hpp>
static void render_ws(const web::Request& req, web::State<Cart> cart, web::ws::Conn& conn) {
    (void)req; (void)cart; (void)conn;
}
static auto route_guards() -> std::vector<web::Middleware> { return {}; }
