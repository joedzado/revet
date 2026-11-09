// Default entry point for a Revet app. An app that needs its own can still ship src/main.cpp.
#include <revet/log.hpp>
#include <revet/web.hpp>
#include <vector>

namespace routes {
void setup(std::vector<web::Route>& out);
}

// Optional: an app defines site_init() (app/routes/site_init.cpp) to register middleware and set up data.
[[gnu::weak]] void site_init() {}

int main(int argc, char** argv) {
    web::init(argc, argv);
    ::log::set_level(::log::Level::INFO);
    ::log::init_from_env();
    site_init();
    std::vector<web::Route> all_routes;
    routes::setup(all_routes);
    web::listen(all_routes);
    return 0;
}
