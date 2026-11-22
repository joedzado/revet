#include "nested.hpp"
#include <revet/web.hpp>
static auto render_get(const web::Request& req) -> web::Task<std::vector<Item>> {
    co_return std::vector<Item>{};
}
static auto render_put(const shop::catalog::Item& body) -> web::Task<Item> { co_return body; }
static auto render_delete(const web::Request& req) -> web::Response { return {}; }
static auto render_patch(const Item& body) -> web::Response { return {}; }
