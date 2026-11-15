#include <revet/web.hpp>
#include <vector>

namespace routes { void setup(std::vector<web::Route>& out); }
void site_init();

auto main(int argc, char** argv) -> int {
    try {
        web::init(argc, argv);
        site_init();

        std::vector<web::Route> all_routes;
        routes::setup(all_routes);

        web::listen(all_routes);
    } catch (...) {
        return -1;
    }
    return 0;
}
