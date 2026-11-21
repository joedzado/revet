// revet-codegen: runs one codegen step for a Revet project.
//   usage: revet-codegen <generator> <project-root> <framework-root>
// Each generator reads the project and writes into <project-root>/build/generated.

#include <cstdio>
#include <functional>
#include <iostream>
#include <map>

#include "common.hpp"

namespace {

using revet::codegen::Ctx;

auto generators() -> const std::map<std::string, std::function<int(const Ctx&)>>& {
    static const std::map<std::string, std::function<int(const Ctx&)>> table = {
        {"gen_json_models", revet::codegen::gen_json_models},
        {"gen_states", revet::codegen::gen_states},
        {"gen_forms", revet::codegen::gen_forms},
        {"gen_components", revet::codegen::gen_components},
        {"gen_routes", revet::codegen::gen_routes},
        {"gen_resources", revet::codegen::gen_resources},
        {"gen_queries", revet::codegen::gen_queries},
        {"gen_api", revet::codegen::gen_api},
        {"gen_styles", revet::codegen::gen_styles},
        {"gen_toggles", revet::codegen::gen_toggles},
        {"gen_uploads", revet::codegen::gen_uploads},
        {"gen_dependencies", revet::codegen::gen_dependencies},
        {"gen_i18n", revet::codegen::gen_i18n},
        {"gen_htmx", revet::codegen::gen_htmx},
        {"gen_optimistic", revet::codegen::gen_optimistic},
        {"gen_scripts", revet::codegen::gen_scripts},
        {"gen_server", revet::codegen::gen_server},
    };
    return table;
}

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc != 4) {
        std::cerr << "usage: revet-codegen <generator> <project-root> <framework-root>\n";
        return 2;
    }
    try {
        const auto& table = generators();
        auto it = table.find(argv[1]);
        if (it == table.end()) {
            std::cerr << "revet-codegen: unknown generator '" << argv[1] << "'\n";
            return 2;
        }
        Ctx ctx{.proj = std::filesystem::path(argv[2]), .framework = std::filesystem::path(argv[3])};
        return it->second(ctx);
    } catch (const revet::codegen::GenError& e) {
        std::cerr << e.what() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "revet-codegen: " << argv[1] << ": " << e.what() << "\n";
    } catch (...) {
        std::cerr << "revet-codegen: " << argv[1] << ": unexpected error\n";
    }
    return 1;
}
