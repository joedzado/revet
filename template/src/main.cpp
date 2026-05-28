#include <revet/web.hpp>
#include <revet/log.hpp>
#include "routes.hpp"
#include "logging_config.hpp"
#include <iostream>
#include <vector>

static const log::TuTag LOG(log::tags::FRAMEWORK);

namespace routes {
    void setup(std::vector<web::Route>& out);
}

// Site-specific initialization (defined in src/site_init.cpp)
void site_init();

int main(int argc, char** argv) {
    web::init(argc, argv);

    log_config::init();
    log::init_from_env();

    site_init();

    std::vector<web::Route> all_routes;
    routes::setup(all_routes);

    web::listen(all_routes);

    return 0;
}
