#include <revet/log.hpp>
#include <revet/web.hpp>

#include "json_models.hpp"

namespace {
const log::TuTag LOG(log::tags::ROUTES);

auto render_post(const web::Request& /*req*/) -> web::Response {
    log::info("Logout");
    auto resp = web::redirect(urls::login);
    resp.cookies.clear<Session>();
    return resp;
}
} // namespace
