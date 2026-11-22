// [[revet::export]] functions -> components.hpp declarations.
#include <cctype>

#include "common.hpp"

namespace revet::codegen {
namespace {

// str.split() with no arguments.
auto split_ws(const std::string& s) -> Lines {
    Lines out;
    std::string cur;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (!cur.empty()) {
                out.push_back(cur);
            }
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

constexpr std::string_view k_export_attr = "[[revet::export]]";

} // namespace

auto gen_components(const Ctx& ctx) -> int {
    fs::path out_path = setup(ctx, "components.hpp");
    fs::path components_dir = ctx.proj / "app" / "components";
    fs::path layouts_dir = ctx.proj / "app" / "layouts";
    fs::path models_dir = ctx.proj / "models";

    std::vector<fs::path> cpp_files = glob(components_dir, ".cpp");
    for (auto& f : glob(layouts_dir, ".cpp")) {
        cpp_files.push_back(f);
    }

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
    for (const auto& hpp : glob(components_dir, ".hpp")) {
        out.push_back(rel_include(hpp, ctx.proj));
    }
    for (const auto& hpp : glob(ctx.proj / "types", ".hpp")) {
        out.push_back(rel_include(hpp, ctx.proj));
    }
    out.emplace_back("");

    struct Deps {
        std::string name;
        std::vector<std::pair<std::string, std::string>> items; // (mode, type)
    };
    std::vector<Deps> comp_deps; // insertion-ordered

    for (const auto& cpp_file : cpp_files) {
        std::string content = read_text(cpp_file);
        for (size_t at = content.find(k_export_attr); at != std::string::npos;
             at = content.find(k_export_attr, at + k_export_attr.size())) {
            size_t start = at + k_export_attr.size();
            size_t paren_open = content.find('(', start);
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

            size_t paren_close = content.find(')', paren_open);
            if (paren_close == std::string::npos) {
                continue;
            }
            std::string args_str = strip(content.substr(paren_open + 1, paren_close - paren_open - 1));

            // auto name(...) -> Type: the return type follows the arrow.
            if (ret_type == "auto") {
                size_t body = content.find_first_of("{;", paren_close);
                size_t arrow = content.find("->", paren_close);
                if (arrow != std::string::npos && arrow < body) {
                    ret_type = strip(content.substr(arrow + 2, body - arrow - 2));
                }
            }

            // State/Flash arguments get a default so callers may omit them.
            Lines header_args;
            std::vector<std::pair<std::string, std::string>> deps;
            for (const auto& raw : split_args(args_str)) {
                std::string arg = strip(raw);
                if (arg.empty()) {
                    continue;
                }
                std::string arg_base = strip(arg.substr(0, arg.find('=')));
                if (auto sm = ctre::search<R"RE(web::(State|Flash)<(\w+)>)RE">(arg_base)) {
                    std::string mode = sm.get<1>().to_string();
                    std::string tname = sm.get<2>().to_string();
                    header_args.push_back(cat(arg_base, " = web::", mode, "<", tname, ">::current()"));
                    deps.emplace_back(mode, tname);
                } else {
                    header_args.push_back(arg);
                }
            }

            size_t line_no = count_newlines(content, start) + 1;
            out.push_back(subst(export_t, {{"line_no", std::to_string(line_no)},
                                           {"cpp_file", cpp_file.string()},
                                           {"ret_type", ret_type},
                                           {"name", name},
                                           {"args_str", join(header_args, ", ")}}));

            if (!deps.empty()) {
                bool replaced = false;
                for (auto& d : comp_deps) {
                    if (d.name == name) {
                        d.items = deps;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    comp_deps.push_back({.name = name, .items = deps});
                }
            }
        }
    }

    out.emplace_back("");
    out.emplace_back("namespace web::components {");
    for (const auto& d : comp_deps) {
        std::string dep_list;
        for (size_t i = 0; i < d.items.size(); ++i) {
            if (i != 0U) {
                dep_list += ", ";
            }
            dep_list += cat("\"", d.items[i].first, "\", \"", d.items[i].second, "\"");
        }
        out.push_back(subst(traits_t, {{"name", d.name}, {"dep_list", dep_list}}));
    }
    out.emplace_back("}");
    write_lines(out_path, out);
    return 0;
}

} // namespace revet::codegen
