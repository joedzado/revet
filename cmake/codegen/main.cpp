// revet-codegen: runs one codegen step for a Revet project.
//   usage: revet-codegen <generator> <project-root> <framework-root>
// Each generator reads the project and writes into <project-root>/build/generated.

#include "common.hpp"

#include <cstdio>
#include <functional>
#include <iostream>
#include <map>

namespace {

using revet::codegen::Ctx;

const std::map<std::string, std::function<int(const Ctx&)>>& generators() {
    using namespace revet::codegen;
    static const std::map<std::string, std::function<int(const Ctx&)>> table = {
        {"gen_json_models", gen_json_models}, {"gen_states", gen_states},         {"gen_forms", gen_forms},
        {"gen_components", gen_components},   {"gen_routes", gen_routes},         {"gen_resources", gen_resources},
        {"gen_queries", gen_queries},         {"gen_api", gen_api},               {"gen_styles", gen_styles},
        {"gen_toggles", gen_toggles},         {"gen_uploads", gen_uploads},       {"gen_dependencies", gen_dependencies},
        {"gen_i18n", gen_i18n},               {"gen_htmx", gen_htmx},             {"gen_optimistic", gen_optimistic},
    };
    return table;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: revet-codegen <generator> <project-root> <framework-root>\n";
        return 2;
    }
    const auto& table = generators();
    auto it = table.find(argv[1]);
    if (it == table.end()) {
        std::cerr << "revet-codegen: unknown generator '" << argv[1] << "'\n";
        return 2;
    }
    Ctx ctx{std::filesystem::path(argv[2]), std::filesystem::path(argv[3])};
    try {
        return it->second(ctx);
    } catch (const revet::codegen::GenError& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "revet-codegen: " << argv[1] << ": " << e.what() << "\n";
        return 1;
    }
}
