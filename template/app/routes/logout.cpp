#include <revet/web.hpp>
#include <revet/log.hpp>
#include "json_models.hpp"

static const log::TuTag LOG(log::tags::ROUTES);

static auto render_post(const web::Request& /*req*/) -> web::Response {
    log::info("Logout");
    auto resp = web::redirect(urls::login);
    resp.cookies.clear<Session>();
    return resp;
}
