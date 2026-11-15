// [[revet::form]] structs -> forms.hpp (populate / validate helpers).
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

std::vector<FormStruct> find_form_structs(const std::string& src) {
    static const Re pattern(R"RE(struct\s+\[\[revet::form\]\]\s+(\w+)\s*\{)RE");
    std::vector<FormStruct> out;
    for (auto it = std::sregex_iterator(src.begin(), src.end(), pattern); it != std::sregex_iterator(); ++it) {
        const std::smatch& m = *it;
        size_t start = static_cast<size_t>(m.position(0) + m.length(0));
        int depth = 1;
        size_t i = start;
        while (i < src.size() && depth > 0) {
            if (src[i] == '{') ++depth;
            else if (src[i] == '}') --depth;
            ++i;
        }
        // Drops the closing brace.
        out.push_back({m[1].str(), src.substr(start, i - 1 - start)});
    }
    return out;
}

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::vector<Member> parse_members(const std::string& body) {
    static const Re name_re(R"RE((\w+)\s*$)RE");
    static const Re init_re(R"RE(\{[^}]*\})RE");
    std::vector<Member> members;
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && is_ws(body[i])) ++i;
        if (i >= body.size()) break;
        int depth = 0;
        size_t j = i;
        while (j < body.size()) {
            char c = body[j];
            if (c == '<' || c == '(') ++depth;
            else if (c == '>' || c == ')') --depth;
            else if (c == ';' && depth == 0) break;
            ++j;
        }
        std::string decl = strip(body.substr(i, j - i));
        i = j + 1;
        if (decl.empty() || decl.find("revet::Field") == std::string::npos) continue;
        bool optional = starts_with(decl, "std::optional");
        std::string no_init = strip(std::regex_replace(decl, init_re, ""));
        Match m;
        if (std::regex_search(no_init, m, name_re)) members.push_back({m[1].str(), optional});
    }
    return members;
}

}  // namespace

int gen_forms(const Ctx& ctx) {
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

    std::string includes;
    for (size_t i = 0; i < form_files.size(); ++i) {
        if (i) includes += "\n";
        includes += rel_include(form_files[i], ctx.proj);
    }

    Lines functions;
    for (const auto& hpp : form_files) {
        std::string src = read_text(hpp);
        for (const auto& fs_struct : find_form_structs(src)) {
            auto members = parse_members(fs_struct.body);
            if (members.empty()) continue;

            Lines pop_lines;
            Lines val_lines;
            for (const auto& m : members) {
                pop_lines.push_back(subst(m.optional ? populate_opt_t : populate_req_t, {{"mname", m.name}}));
                val_lines.push_back(subst(m.optional ? validate_opt_t : validate_req_t, {{"mname", m.name}}));
            }
            functions.push_back(subst(populate_fn_t, {{"struct_name", fs_struct.name}, {"body", join(pop_lines, "\n")}}));
            functions.push_back(subst(validate_fn_t, {{"struct_name", fs_struct.name}, {"body", join(val_lines, "\n")}}));
        }
    }

    write_file(out_path,
               subst(header_t, {{"includes", includes}, {"functions", join(functions, "\n\n")}}) + "\n");
    return 0;
}

}  // namespace revet::codegen
