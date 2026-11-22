#include "users_data.hpp"

// GET /api/v1/users — return all users as a JSON array
static auto render_get(const web::Request& /*req*/) -> web::Response {
    std::string out = "[";
    bool first = true;
    for (const auto& [id, info] : USERS) {
        const auto& [name, email] = info;
        if (!first) out += ",";
        first = false;
        out += revet::json::to_json<User>(User{.id = id, .name = name, .email = email});
    }
    out += "]";
    return { .body = std::move(out), .content_type = "application/json" };
}

// POST /api/v1/users — CreateUser body → User response (typed model param)
// Framework validates Content-Type, body presence, and required fields before calling this.
static auto render_post(const CreateUser& body) -> User {
    auto id = std::to_string(USERS.size() + 1);
    return User{ .id = id, .name = body.name, .email = body.email, .role = body.role };
}

static auto api_guards() -> std::vector<web::Middleware> {
    return { web::api::bearer([](std::string_view tok) {
        return tok == "demo-key";
    })};
}
