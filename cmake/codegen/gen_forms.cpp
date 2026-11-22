// [[revet::form]] structs -> forms.hpp (populate / validate helpers).
#include <algorithm>
#include <iterator>
#include <ranges>

#include "common.hpp"

namespace revet::codegen {
namespace {

struct Member {
    std::string name;
    bool optional;
};

struct FormStruct {
    std::string name;
    std::string body;
};

auto find_form_structs(const std::string& src) -> std::vector<FormStruct> {
    std::vector<FormStruct> out;
    for (auto m : ctre::search_all<R"RE(struct\s+\[\[revet::form\]\]\s+(\w+)\s*\{)RE">(std::string_view(src))) {
        size_t start = offset_in(src, m.to_view()) + m.size();
        int depth = 1;
        size_t i = start;
        while (i < src.size() && depth > 0) {
            if (src[i] == '{') {
                ++depth;
            } else if (src[i] == '}') {
                --depth;
            }
            ++i;
        }
        // Drops the closing brace.
        out.push_back({.name = m.get<1>().to_string(), .body = src.substr(start, i - 1 - start)});
    }
    return out;
}

auto parse_members(const std::string& body) -> std::vector<Member> {
    std::vector<Member> members;
    for (size_t i = body.find_first_not_of(" \t\n\r"); i < body.size(); i = body.find_first_not_of(" \t\n\r", i)) {
        int depth = 0;
        size_t j = i;
        while (j < body.size()) {
            char c = body[j];
            if (c == '<' || c == '(') {
                ++depth;
            } else if (c == '>' || c == ')') {
                --depth;
            } else if (c == ';' && depth == 0) {
                break;
            }
            ++j;
        }
        std::string decl = strip(body.substr(i, j - i));
        i = j + 1;
        if (decl.empty() || !decl.contains("revet::Field")) {
            continue;
        }
        bool optional = starts_with(decl, "std::optional");
        std::string no_init = strip(replace_all<R"RE(\{[^}]*\})RE">(decl, ""));
        if (auto m = ctre::search<R"RE((\w+)\s*$)RE">(no_init)) {
            members.push_back({.name = m.get<1>().to_string(), .optional = optional});
        }
    }
    return members;
}

} // namespace

auto gen_forms(const Ctx& ctx) -> int {
    fs::path out_path = setup(ctx, "forms.hpp");
    fs::path forms_dir = ctx.proj / "app" / "forms";
    std::vector<fs::path> form_files = glob(forms_dir, ".hpp");

    if (form_files.empty()) {
        write_file(out_path, "// no forms/*.hpp files found\n#pragma once\nnamespace forms {}\n");
        return 0;
    }

    const std::string header_t = tmpl_whole(ctx, "forms_header.tmpl");
    const std::string populate_fn_t = tmpl(ctx, "forms_populate_fn.tmpl");
    const std::string validate_fn_t = tmpl(ctx, "forms_validate_fn.tmpl");
    const std::string populate_opt_t = tmpl(ctx, "forms_populate_optional.tmpl");
    const std::string populate_req_t = tmpl(ctx, "forms_populate_required.tmpl");
    const std::string validate_opt_t = tmpl(ctx, "forms_validate_optional.tmpl");
    const std::string validate_req_t = tmpl(ctx, "forms_validate_required.tmpl");

    std::string includes = join(form_files | std::views::transform([&](const fs::path& f) -> std::string {
                                    return rel_include(f, ctx.proj);
                                }) | std::ranges::to<Lines>(),
                                "\n");

    Lines functions;
    for (const auto& hpp : form_files) {
        std::string src = read_text(hpp);
        for (const auto& fs_struct : find_form_structs(src)) {
            auto members = parse_members(fs_struct.body);
            if (members.empty()) {
                continue;
            }

            // One line per member, from the optional or required template.
            auto body = [&](const std::string& opt_t, const std::string& req_t) -> std::string {
                return join(members | std::views::transform([&](const Member& m) -> std::string {
                                return subst(m.optional ? opt_t : req_t, {{"mname", m.name}});
                            }) | std::ranges::to<Lines>(),
                            "\n");
            };
            functions.push_back(subst(
                populate_fn_t, {{"struct_name", fs_struct.name}, {"body", body(populate_opt_t, populate_req_t)}}));
            functions.push_back(subst(
                validate_fn_t, {{"struct_name", fs_struct.name}, {"body", body(validate_opt_t, validate_req_t)}}));
        }
    }

    write_file(out_path, subst(header_t, {{"includes", includes}, {"functions", join(functions, "\n\n")}}) + "\n");
    return 0;
}

} // namespace revet::codegen
