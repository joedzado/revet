// [[revet::export]] functions -> components.hpp declarations.
#include <algorithm>
#include <cctype>
#include <iterator>
#include <ranges>

#include "common.hpp"

namespace revet::codegen {
namespace {

// str.split() with no arguments.
auto split_ws(const std::string& s) -> Lines {
    return captures<R"RE((\S+))RE">(s);
}

constexpr std::string_view k_export_attr = "[[revet::export]]";

} // namespace

auto gen_components(const Ctx& ctx) -> int {
    const fs::path out_path = setup(ctx, "components.hpp");
    const fs::path components_dir = ctx.proj / "app" / "components";
    const fs::path layouts_dir = ctx.proj / "app" / "layouts";
    const fs::path models_dir = ctx.proj / "models";

    std::vector<fs::path> cpp_files = glob(components_dir, ".cpp");
    std::ranges::copy(glob(layouts_dir, ".cpp"), std::back_inserter(cpp_files));

    if (cpp_files.empty()) {
        write_file(out_path, "// no components/*.cpp or layouts/*.cpp files found\n#pragma once\n");
        return 0;
    }

    const std::string export_t = tmpl(ctx, "components_export.tmpl");
    const std::string traits_t = tmpl(ctx, "components_traits.tmpl");

    Lines out = {
        "// Auto-generated — do not edit",
        "#pragma once",
        "#include <revet/web.hpp>",
        "#include \"states.hpp\"",
    };
    if (has_models(models_dir)) {
        out.emplace_back("#include \"models.hpp\"");
    }
    const auto include_of = [&](const fs::path& hpp) -> std::string { return rel_include(hpp, ctx.proj); };
    std::ranges::transform(glob(components_dir, ".hpp"), std::back_inserter(out), include_of);
    std::ranges::transform(glob(ctx.proj / "types", ".hpp"), std::back_inserter(out), include_of);
    out.emplace_back("");

    struct Deps {
        std::string name;
        std::vector<std::pair<std::string, std::string>> items; // (mode, type)
    };
    std::vector<Deps> comp_deps; // insertion-ordered

    for (const auto& cpp_file : cpp_files) {
        const std::string content = read_text(cpp_file);
        for (size_t at = content.find(k_export_attr); at != std::string::npos;
             at = content.find(k_export_attr, at + k_export_attr.size())) {
            const size_t start = at + k_export_attr.size();
            const size_t paren_open = content.find('(', start);
            if (paren_open == std::string::npos) {
                continue;
            }

            Lines head_parts = split_ws(content.substr(start, paren_open - start));
            if (head_parts.empty()) {
                continue;
            }
            std::string name = head_parts.back();
            head_parts.pop_back();
            std::string ret_type = join(head_parts, " ");

            const size_t paren_close = content.find(')', paren_open);
            if (paren_close == std::string::npos) {
                continue;
            }
            const std::string args_str = strip(content.substr(paren_open + 1, paren_close - paren_open - 1));

            // auto name(...) -> Type: the return type follows the arrow.
            if (ret_type == "auto") {
                const size_t body = content.find_first_of("{;", paren_close);
                const size_t arrow = content.find("->", paren_close);
                if (arrow != std::string::npos && arrow < body) {
                    ret_type = strip(content.substr(arrow + 2, body - arrow - 2));
                }
            }

            // State/Flash arguments get a default so callers may omit them.
            constexpr auto state_arg = ctre::search<R"RE(web::(State|Flash)<(\w+)>)RE">;
            auto base_of = [](const std::string& arg) -> std::string { return strip(arg.substr(0, arg.find('='))); };
            Lines args = split_args(args_str);
            const auto header_args =
                args | std::views::transform([&](const std::string& arg) -> std::string {
                    std::string base = base_of(arg);
                    const auto sm = state_arg(base);
                    return sm ? cat(base, " = web::", sm.get<1>().to_view(), "<", sm.get<2>().to_view(), ">::current()")
                              : arg;
                }) |
                std::ranges::to<Lines>();
            const auto deps =
                args | std::views::transform(base_of) | std::views::filter([&](const std::string& base) -> bool {
                    return static_cast<bool>(state_arg(base));
                }) |
                std::views::transform([&](const std::string& base) -> std::pair<std::string, std::string> {
                    const auto sm = state_arg(base);
                    return {sm.get<1>().to_string(), sm.get<2>().to_string()};
                }) |
                std::ranges::to<std::vector<std::pair<std::string, std::string>>>();

            const size_t line_no = count_newlines(content, start) + 1;
            out.push_back(subst(export_t, {{"line_no", std::to_string(line_no)},
                                           {"cpp_file", cpp_file.string()},
                                           {"ret_type", ret_type},
                                           {"name", name},
                                           {"args_str", join(header_args, ", ")}}));

            if (!deps.empty()) {
                if (const auto it = std::ranges::find(comp_deps, name, &Deps::name); it != comp_deps.end()) {
                    it->items = deps;
                } else {
                    comp_deps.push_back({.name = name, .items = deps});
                }
            }
        }
    }

    out.emplace_back("");
    out.emplace_back("namespace web::components {");
    std::ranges::transform(comp_deps, std::back_inserter(out), [&](const Deps& d) -> std::string {
        std::string dep_list = join(d.items | std::views::transform([](const auto& item) -> std::string {
                                        return cat("\"", item.first, "\", \"", item.second, "\"");
                                    }) | std::ranges::to<Lines>(),
                                    ", ");
        return subst(traits_t, {{"name", d.name}, {"dep_list", dep_list}});
    });
    out.emplace_back("}");
    write_lines(out_path, out);
    return 0;
}

} // namespace revet::codegen
