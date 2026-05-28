#include <revet/web.hpp>
#include <revet/log.hpp>
#include "resources.hpp"
#include "queries.hpp"
#include "json_models.hpp"

static const log::TuTag LOG(log::tags::ROUTES);

static auto render_post(const web::Request& req) -> web::Task<web::Response> {
    const auto id  = req.params.get("id");
    const int  iid = std::stoi(id);
    auto db = resources::users_db<db::Write>();

    const auto rows = co_await queries::users::find_by_id(db, {.id = iid});
    if (rows.empty()) {
        co_return web::redirect(urls::users)
            .flash(Toast{ .message = "User not found.", .kind = "error" });
    }

    co_await queries::users::delete_by_id(db, {.id = iid});
    log::info("Deleted user id={}", id);

    co_return web::redirect(urls::users)
        .flash(Toast{ .message = "User deleted.", .kind = "success" });
}
