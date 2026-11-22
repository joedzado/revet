// Default entry point for a Revet app. An app that needs its own can still ship src/main.cpp.
#include <print>
#include <revet/log.hpp>
#include <revet/web.hpp>
#include <vector>

namespace routes {
void setup(std::vector<web::Route>& out);
}

// Optional: an app defines site_init() (app/routes/site_init.cpp) to register middleware and set up data.
[[gnu::weak]] void site_init() {}

auto main(int argc, char** argv) -> int {
    try {
        web::init(argc, argv);
        web::log::set_level(web::log::Level::INFO);
        web::log::init_from_env();
        site_init();
        std::vector<web::Route> all_routes;
        routes::setup(all_routes);
        web::listen(all_routes);
    } catch (const web::StartupError& e) {
        std::println(stderr, "{}", e.what());
        return e.exit_code;
    }
    return 0;
}
