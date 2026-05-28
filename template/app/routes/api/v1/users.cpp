#include <revet/web.hpp>

static const log::TuTag LOG(log::tags::ROUTES);

// GET /api/v1/users
// API route handlers run synchronously. If you need async work (DB, HTTP),
// do it ahead of time or use a non-/api route that supports web::Task<web::Response>.
static auto render_get(const web::Request& /*req*/) -> web::Response {
    return web::Response{
        .body         = "[]",
        .content_type = "application/json",
    };
}

// POST /api/v1/users — typed model: framework validates Content-Type, body, required fields.
static auto render_post(const CreateUser& body) -> User {
    return User{ .id = "new", .name = body.name, .email = body.email };
}

static auto api_guards() -> std::vector<web::Middleware> {
    return { web::api::bearer([](std::string_view tok) -> bool {
        const char* key = std::getenv("API_KEY");
        return key != nullptr && tok == key;
    })};
}
