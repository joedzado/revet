// Port of cmake/gen_json_models.py: [[revet::model]] structs -> models.hpp (from_json / from_row / to_json).
#include "common.hpp"

#include <cctype>

namespace revet::codegen {
namespace {

const Re& attr_re() {
    static const Re re(R"RE(\[\[\s*revet::model(?:\s*\(\s*[^)]*\s*\))?\s*\]\]\s*struct|struct\s*\[\[\s*revet::model(?:\s*\(\s*[^)]*\s*\))?\s*\]\])RE"
                       R"RE(\s+(\w+)\s*\{([^}]*)\}\s*;)RE");
    return re;
}

const Re& ns_open_re() {
    static const Re re(R"RE(\bnamespace\s+([\w:]+)\s*\{)RE");
    return re;
}

const Re& field_stmt_re() {
    static const Re re(R"RE(([\w:<>,\s]+?)\s+(\w+))RE");
    return re;
}

struct Field {
    std::string type;
    std::string name;
};

struct Model {
    std::string src_file;
    std::string qualified;
    std::vector<Field> fields;
};

// Python: re.sub(r"\s*=\s*.*$", "", line) for a single-line statement.
std::string drop_initializer(const std::string& line) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return line;
    if (line.find('\n', eq) != std::string::npos) return line;
    size_t cut = eq;
    while (cut > 0 && (line[cut - 1] == ' ' || line[cut - 1] == '\t' || line[cut - 1] == '\n' || line[cut - 1] == '\r')) --cut;
    return line.substr(0, cut);
}

std::vector<Field> parse_fields(const std::string& body) {
    std::vector<Field> fields;
    std::string piece;
    auto handle = [&](const std::string& raw) {
        std::string line = strip(raw);
        if (line.empty()) return;
        if (line.find('(') != std::string::npos || line.find('{') != std::string::npos) return;
        line = strip(drop_initializer(line));
        Match m;
        if (match_full(line, m, field_stmt_re())) {
            fields.push_back({collapse_ws(strip(m[1].str())), m[2].str()});
        }
    };
    for (char c : body) {
        if (c == ';') {
            handle(piece);
            piece.clear();
        } else {
            piece += c;
        }
    }
    handle(piece);
    return fields;
}

// Namespace that encloses `pos` (Python _ns_at): a brace-depth walk over the prefix.
std::string ns_at(const std::string& content, size_t pos) {
    struct Frame {
        std::string name;
        int depth;
    };
    std::vector<Frame> stack;
    int depth = 0;
    size_t i = 0;
    auto is_word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    while (i < pos) {
        if (content.compare(i, 9, "namespace") == 0 && (i == 0 || !is_word(content[i - 1]))) {
            Match m;
            if (std::regex_search(content.begin() + static_cast<std::ptrdiff_t>(i), content.end(), m, ns_open_re(),
                                  std::regex_constants::match_continuous |
                                      (i > 0 ? std::regex_constants::match_prev_avail : std::regex_constants::match_default))) {
                ++depth;
                stack.push_back({m[1].str(), depth});
                i += static_cast<size_t>(m.length(0));
                continue;
            }
        }
        char ch = content[i];
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            while (!stack.empty() && stack.back().depth > depth) stack.pop_back();
        }
        ++i;
    }
    std::string out;
    for (const auto& f : stack) {
        // Split "a::b" and re-join with "::", as the Python version does.
        std::string name = f.name;
        if (!out.empty()) out += "::";
        out += name;
    }
    return out;
}

bool is_optional(const std::string& t) { return starts_with(t, "std::optional<"); }
bool is_vector(const std::string& t) { return starts_with(t, "std::vector<"); }

std::string inner_type(const std::string& t) {
    size_t lt = t.find('<');
    size_t gt = t.rfind('>');
    return strip(t.substr(lt + 1, gt - lt - 1));
}

std::string deser_expr(const std::string& ftype, const std::string& value_expr) {
    if (is_optional(ftype)) {
        std::string inner = inner_type(ftype);
        Match km;
        static const Re key_re(R"RE(\.at\("([^"]+)"\)$)RE");
        if (std::regex_search(value_expr, km, key_re)) {
            std::string key = km[1].str();
            std::string base = value_expr.substr(0, value_expr.rfind(".at("));
            return cat(base, ".has(\"", key, "\") ? std::optional<", inner, ">(static_cast<", inner, ">(", base,
                       ".at(\"", key, "\"))) : std::nullopt");
        }
    }
    if (is_vector(ftype)) {
        return cat("(", value_expr, ").operator std::vector<", inner_type(ftype), ">()");
    }
    return cat("static_cast<", ftype, ">(", value_expr, ")");
}

std::string row_expr(const std::string& ftype, const std::string& cell) {
    if (ftype == "std::string" || ftype == "std::string_view") return cell;
    if (ftype == "bool") return cat("(", cell, " == \"1\" || ", cell, " == \"true\")");
    if (ftype == "float") return cat("std::stof(", cell, ")");
    if (ftype == "double") return cat("std::stod(", cell, ")");
    if (ftype == "int" || ftype == "int32_t" || ftype == "uint32_t" || ftype == "short") {
        return cat("static_cast<", ftype, ">(std::stol(", cell, "))");
    }
    if (ftype == "int64_t" || ftype == "long" || ftype == "long long" || ftype == "uint64_t" || ftype == "size_t" ||
        ftype == "std::size_t") {
        return cat("static_cast<", ftype, ">(std::stoll(", cell, "))");
    }
    if (is_optional(ftype)) {
        std::string inner = inner_type(ftype);
        return cat("(", cell, ".empty() ? std::optional<", inner, ">{} : std::optional<", inner, ">(",
                   row_expr(inner, cell), "))");
    }
    return cat(ftype, "{}");
}

// Lines that append the JSON encoding of `expr` to `out_var` (Python ser_lines).
Lines ser_lines(const std::string& ftype, const std::string& expr, const std::string& out_var) {
    Lines lines;
    if (is_optional(ftype)) {
        std::string inner = inner_type(ftype);
        lines.push_back(cat("    if (", expr, ".has_value()) {"));
        for (const auto& ln : ser_lines(inner, cat("(*", expr, ")"), out_var)) lines.push_back("    " + ln);
        lines.push_back("    } else {");
        lines.push_back(cat("        ", out_var, " += \"null\";"));
        lines.push_back("    }");
        return lines;
    }
    if (is_vector(ftype)) {
        std::string inner = inner_type(ftype);
        lines.push_back(cat("    ", out_var, " += \"[\";"));
        lines.push_back("    {");
        lines.push_back("        bool first = true;");
        lines.push_back(cat("        for (auto const& _elem : ", expr, ") {"));
        lines.push_back(cat("            if (!first) ", out_var, " += \",\";"));
        lines.push_back("            first = false;");
        for (const auto& ln : ser_lines(inner, "_elem", out_var)) lines.push_back("    " + ln);
        lines.push_back("        }");
        lines.push_back("    }");
        lines.push_back(cat("    ", out_var, " += \"]\";"));
        return lines;
    }
    if (ftype == "std::string" || ftype == "std::string_view") {
        lines.push_back(cat("    ", out_var, " += revet::json::detail::quote(", expr, ");"));
        return lines;
    }
    if (ftype == "bool") {
        lines.push_back(cat("    ", out_var, " += (", expr, ") ? \"true\" : \"false\";"));
        return lines;
    }
    lines.push_back(cat("    ", out_var, " += std::to_string(", expr, ");"));
    return lines;
}

std::vector<fs::path> model_sources(const Ctx& ctx) {
    std::vector<fs::path> files = glob(ctx.proj / "models", ".hpp");
    for (auto& f : glob(ctx.proj / "app" / "apis", ".hpp")) files.push_back(f);
    return files;
}

}  // namespace

int gen_json_models(const Ctx& ctx) {
    fs::path out_path = setup(ctx, "models.hpp");
    std::vector<fs::path> sources = model_sources(ctx);

    std::vector<Model> models;
    for (const auto& f : sources) {
        std::string content = read_text(f);
        auto begin = std::sregex_iterator(content.begin(), content.end(), attr_re());
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            const std::smatch& m = *it;
            std::string name = m[1].str();
            std::string body = m[2].str();
            std::string ns = ns_at(content, static_cast<size_t>(m.position(0)));
            Model model;
            model.src_file = f.filename().string();
            model.qualified = ns.empty() ? name : ns + "::" + name;
            model.fields = parse_fields(body);
            models.push_back(std::move(model));
        }
    }

    Lines lines = file_header({"#include <revet/json.hpp>", "#include <string>", "#include <string_view>",
                               "#include <vector>", "#include <optional>", "#include <cstdio>"});
    for (const auto& hpp : sources) lines.push_back(rel_include(hpp, ctx.proj));

    // Preamble template: static_tmpl() strips trailing newlines; splitlines() keeps the leading blank line.
    std::string preamble = tmpl(ctx, "json_models_preamble.tmpl");
    for (const auto& ln : splitlines(preamble)) lines.push_back(ln);
    lines.push_back("");

    const std::string from_json_value_t = tmpl(ctx, "json_from_json_value.tmpl");
    const std::string from_json_t = tmpl(ctx, "json_from_json.tmpl");
    const std::string from_row_t = tmpl(ctx, "json_from_row.tmpl");
    const std::string to_json_t = tmpl(ctx, "json_to_json.tmpl");

    for (const auto& model : models) {
        std::string name = model.qualified;

        std::string fv_body;
        for (const auto& fd : model.fields) {
            if (!fv_body.empty()) fv_body += "\n";
            fv_body += cat("    out.", fd.name, " = ", deser_expr(fd.type, cat("v.at(\"", fd.name, "\")")), ";");
        }
        lines.push_back(subst(from_json_value_t, {{"name", name}, {"src_file", model.src_file}, {"body", fv_body}}));
        lines.push_back("");
        lines.push_back(subst(from_json_t, {{"name", name}}));
        lines.push_back("");

        std::string fr_body;
        for (size_t i = 0; i < model.fields.size(); ++i) {
            const auto& fd = model.fields[i];
            if (i) fr_body += "\n";
            fr_body += cat("    out.", fd.name, " = ", row_expr(fd.type, cat("r[", std::to_string(i), "]")), ";");
        }
        lines.push_back(subst(from_row_t, {{"name", name}, {"body", fr_body}}));
        lines.push_back("");

        Lines tj_lines;
        for (size_t i = 0; i < model.fields.size(); ++i) {
            const auto& fd = model.fields[i];
            std::string sep = i > 0 ? "," : "";
            tj_lines.push_back(cat("    out += \"", sep, "\\\"", fd.name, "\\\":\";"));
            for (const auto& ln : ser_lines(fd.type, cat("val.", fd.name), "out")) tj_lines.push_back(ln);
        }
        lines.push_back(subst(to_json_t, {{"name", name}, {"body", join(tj_lines, "\n")}}));
        lines.push_back("");
    }

    lines.push_back("} // namespace revet::json");
    lines.push_back("");
    write_file(out_path, join(lines, "\n"));
    return 0;
}

}  // namespace revet::codegen
