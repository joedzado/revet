// [[revet::model]] structs -> models.hpp (from_json / from_row / to_json).
#include <algorithm>
#include <cctype>
#include <iterator>
#include <optional>
#include <ranges>

#include "common.hpp"

namespace revet::codegen {
namespace {

// `[[revet::model]] struct Name { ... };` or `struct [[revet::model]] Name { ... };`
// Captures: 1 = name, 2 = body.
constexpr auto find_models = ctre::search_all<
    R"RE((?:\[\[\s*revet::model(?:\s*\(\s*[^)]*\s*\))?\s*\]\]\s*struct|struct\s*\[\[\s*revet::model(?:\s*\(\s*[^)]*\s*\))?\s*\]\]))RE"
    R"RE(\s+(\w+)\s*\{([^}]*)\}\s*;)RE">;

struct Field {
    std::string type;
    std::string name;
};

struct Model {
    std::string src_file;
    std::string qualified;
    std::vector<Field> fields;
};

// Strips a trailing `= ...` initializer from a single-line statement.
auto drop_initializer(const std::string& line) -> std::string {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
        return line;
    }
    if (line.find('\n', eq) != std::string::npos) {
        return line;
    }
    const size_t last = eq == 0 ? std::string::npos : line.find_last_not_of(" \t\n\r", eq - 1);
    return line.substr(0, last == std::string::npos ? 0 : last + 1);
}

auto parse_fields(const std::string& body) -> std::vector<Field> {
    // One statement per ';'. Functions and nested braces are not fields.
    auto field_of = [](auto piece) -> std::optional<Field> {
        std::string line = strip(std::string_view(piece));
        if (line.empty() || line.contains('(') || line.contains('{')) {
            return std::nullopt;
        }
        line = strip(drop_initializer(line));
        if (const auto m = ctre::match<R"RE(([\w:<>,\s]+?)\s+(\w+))RE">(line)) {
            return Field{.type = collapse_ws(strip(m.get<1>().to_view())), .name = m.get<2>().to_string()};
        }
        return std::nullopt;
    };
    return body | std::views::split(';') | std::views::transform(field_of) |
           std::views::filter([](const std::optional<Field>& f) -> bool { return f.has_value(); }) |
           std::views::transform([](const std::optional<Field>& f) -> Field { return *f; }) |
           std::ranges::to<std::vector<Field>>();
}

// Namespace that encloses `pos`: a brace-depth walk over the prefix.
auto ns_at(const std::string& content, size_t pos) -> std::string {
    struct Frame {
        std::string name;
        int depth;
    };
    std::vector<Frame> stack;
    int depth = 0;
    size_t i = 0;
    const auto is_word = [](char c) -> bool { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    while (i < pos) {
        if (content.compare(i, 9, "namespace") == 0 && (i == 0 || !is_word(content.at(i - 1)))) {
            if (const auto m =
                    ctre::starts_with<R"RE(namespace\s+([\w:]+)\s*\{)RE">(std::string_view(content).substr(i))) {
                ++depth;
                stack.push_back({.name = m.get<1>().to_string(), .depth = depth});
                i += m.get<0>().size();
                continue;
            }
        }
        const char ch = content.at(i);
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            while (!stack.empty() && stack.back().depth > depth) {
                stack.pop_back();
            }
        }
        ++i;
    }
    return join(stack | std::views::transform(&Frame::name) | std::ranges::to<Lines>(), "::");
}

auto is_optional(const std::string& t) -> bool {
    return starts_with(t, "std::optional<");
}
auto is_vector(const std::string& t) -> bool {
    return starts_with(t, "std::vector<");
}

auto inner_type(const std::string& t) -> std::string {
    const size_t lt = t.find('<');
    const size_t gt = t.rfind('>');
    return strip(t.substr(lt + 1, gt - lt - 1));
}

auto deser_expr(const std::string& ftype, const std::string& value_expr) -> std::string {
    if (is_optional(ftype)) {
        const std::string inner = inner_type(ftype);
        if (const auto km = ctre::search<R"RE(\.at\("([^"]+)"\)$)RE">(value_expr)) {
            const std::string key = km.get<1>().to_string();
            const std::string base = value_expr.substr(0, value_expr.rfind(".at("));
            return cat(base, ".has(\"", key, "\") ? std::optional<", inner, ">(static_cast<", inner, ">(", base,
                       ".at(\"", key, "\"))) : std::nullopt");
        }
    }
    if (is_vector(ftype)) {
        return cat("(", value_expr, ").operator std::vector<", inner_type(ftype), ">()");
    }
    return cat("static_cast<", ftype, ">(", value_expr, ")");
}

auto row_expr(const std::string& ftype, const std::string& cell) -> std::string {
    if (ftype == "std::string" || ftype == "std::string_view") {
        return cell;
    }
    if (ftype == "bool") {
        return cat("(", cell, " == \"1\" || ", cell, " == \"true\")");
    }
    if (ftype == "float") {
        return cat("std::stof(", cell, ")");
    }
    if (ftype == "double") {
        return cat("std::stod(", cell, ")");
    }
    if (ftype == "int" || ftype == "int32_t" || ftype == "uint32_t" || ftype == "short") {
        return cat("static_cast<", ftype, ">(std::stol(", cell, "))");
    }
    if (ftype == "int64_t" || ftype == "long" || ftype == "long long" || ftype == "uint64_t" || ftype == "size_t" ||
        ftype == "std::size_t") {
        return cat("static_cast<", ftype, ">(std::stoll(", cell, "))");
    }
    if (is_optional(ftype)) {
        const std::string inner = inner_type(ftype);
        return cat("(", cell, ".empty() ? std::optional<", inner, ">{} : std::optional<", inner, ">(",
                   row_expr(inner, cell), "))");
    }
    return cat(ftype, "{}");
}

// Lines that append the JSON encoding of `expr` to `out_var` .
auto indented(const Lines& lines) -> Lines {
    return lines | std::views::transform([](const std::string& ln) -> std::string { return "    " + ln; }) |
           std::ranges::to<Lines>();
}

auto ser_lines(const std::string& ftype, const std::string& expr, const std::string& out_var) -> Lines {
    Lines lines;
    if (is_optional(ftype)) {
        const std::string inner = inner_type(ftype);
        lines.push_back(cat("    if (", expr, ".has_value()) {"));
        append(lines, indented(ser_lines(inner, cat("(*", expr, ")"), out_var)));
        lines.emplace_back("    } else {");
        lines.push_back(cat("        ", out_var, " += \"null\";"));
        lines.emplace_back("    }");
        return lines;
    }
    if (is_vector(ftype)) {
        const std::string inner = inner_type(ftype);
        lines.push_back(cat("    ", out_var, " += \"[\";"));
        lines.emplace_back("    {");
        lines.emplace_back("        bool first = true;");
        lines.push_back(cat("        for (auto const& _elem : ", expr, ") {"));
        lines.push_back(cat("            if (!first) ", out_var, " += \",\";"));
        lines.emplace_back("            first = false;");
        append(lines, indented(ser_lines(inner, "_elem", out_var)));
        lines.emplace_back("        }");
        lines.emplace_back("    }");
        lines.push_back(cat("    ", out_var, " += \"]\";"));
        return lines;
    }
    if (ftype == "std::string" || ftype == "std::string_view") {
        lines.push_back(cat("    ", out_var, " += revet::json::detail::quote(", expr, ");"));
        return lines;
    }
    if (ftype == "bool") {
        lines.push_back(cat("    ", out_var, " += (", expr, R"() ? "true" : "false";)"));
        return lines;
    }
    lines.push_back(cat("    ", out_var, " += std::to_string(", expr, ");"));
    return lines;
}

auto model_sources(const Ctx& ctx) -> std::vector<fs::path> {
    std::vector<fs::path> files = glob(ctx.proj / "models", ".hpp");
    std::ranges::copy(glob(ctx.proj / "app" / "apis", ".hpp"), std::back_inserter(files));
    return files;
}

} // namespace

auto gen_json_models(const Ctx& ctx) -> int {
    const fs::path out_path = setup(ctx, "models.hpp");
    std::vector<fs::path> sources = model_sources(ctx);

    std::vector<Model> models;
    for (const auto& f : sources) {
        std::string content = read_text(f);
        std::ranges::transform(
            find_models(std::string_view(content)), std::back_inserter(models), [&](const auto& m) -> Model {
                const std::string name = m.template get<1>().to_string();
                const std::string ns = ns_at(content, offset_in(content, m.template get<0>().to_view()));
                return {.src_file = f.filename().string(),
                        .qualified = ns.empty() ? name : cat(ns, "::", name),
                        .fields = parse_fields(m.template get<2>().to_string())};
            });
    }

    Lines lines = file_header({"#include <revet/json.hpp>", "#include <string>", "#include <string_view>",
                               "#include <vector>", "#include <optional>", "#include <cstdio>"});
    std::ranges::transform(sources, std::back_inserter(lines),
                           [&](const fs::path& hpp) -> std::string { return rel_include(hpp, ctx.proj); });

    // Preamble template: static_tmpl() strips trailing newlines; splitlines() keeps the leading blank line.
    const std::string preamble = tmpl(ctx, "json_models_preamble.tmpl");
    append(lines, splitlines(preamble));
    lines.emplace_back("");

    const std::string from_json_value_t = tmpl(ctx, "json_from_json_value.tmpl");
    const std::string from_json_t = tmpl(ctx, "json_from_json.tmpl");
    const std::string from_row_t = tmpl(ctx, "json_from_row.tmpl");
    const std::string to_json_t = tmpl(ctx, "json_to_json.tmpl");

    for (const auto& model : models) {
        std::string name = model.qualified;

        const auto indexed_fields = std::views::zip(std::views::iota(0UZ), model.fields);
        std::string fv_body =
            join(model.fields | std::views::transform([](const Field& fd) -> std::string {
                     return cat("    out.", fd.name, " = ", deser_expr(fd.type, cat("v.at(\"", fd.name, "\")")), ";");
                 }) | std::ranges::to<Lines>(),
                 "\n");
        lines.push_back(subst(from_json_value_t, {{"name", name}, {"src_file", model.src_file}, {"body", fv_body}}));
        lines.emplace_back("");
        lines.push_back(subst(from_json_t, {{"name", name}}));
        lines.emplace_back("");

        std::string fr_body =
            join(indexed_fields | std::views::transform([](const auto& indexed) -> std::string {
                     const auto& [i, fd] = indexed;
                     return cat("    out.", fd.name, " = ", row_expr(fd.type, cat("r[", std::to_string(i), "]")), ";");
                 }) | std::ranges::to<Lines>(),
                 "\n");
        lines.push_back(subst(from_row_t, {{"name", name}, {"body", fr_body}}));
        lines.emplace_back("");

        // Per field: its key (comma-separated after the first), then its value.
        auto field_json = [](const auto& indexed) -> Lines {
            const auto& [i, fd] = indexed;
            Lines out = {cat("    out += \"", i > 0 ? "," : "", "\\\"", fd.name, R"(\":";)")};
            append(out, ser_lines(fd.type, cat("val.", fd.name), "out"));
            return out;
        };
        const auto tj_lines =
            indexed_fields | std::views::transform(field_json) | std::views::join | std::ranges::to<Lines>();
        lines.push_back(subst(to_json_t, {{"name", name}, {"body", join(tj_lines, "\n")}}));
        lines.emplace_back("");
    }

    lines.emplace_back("} // namespace revet::json");
    lines.emplace_back("");
    write_file(out_path, join(lines, "\n"));
    return 0;
}

} // namespace revet::codegen
