// web::State / web::Flash / toggle types -> states.hpp (cookie codecs).
#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>

#include "common.hpp"

namespace revet::codegen {
namespace {

auto is_str_type(const std::string& t) -> bool {
    return t == "std::string" || t == "std::string_view";
}

auto is_num_type(const std::string& t) -> bool {
    static const std::set<std::string> nums{"int",      "int32_t",  "int64_t", "int16_t", "uint32_t",
                                            "uint64_t", "uint16_t", "size_t",  "float",   "double"};
    std::string_view bare = t;
    if (bare.starts_with("std::")) {
        bare.remove_prefix(5);
    }
    return nums.contains(std::string(bare));
}

// Returns "" for unsupported types.
auto pack_field(const std::string& ft, const std::string& fn) -> std::string {
    if (is_str_type(ft)) {
        return cat("    { uint32_t len = (uint32_t)s.", fn, ".size(); out.append((char*)&len, 4); out.append(s.", fn,
                   "); }");
    }
    if (is_num_type(ft)) {
        return cat("    out.append((char*)&s.", fn, ", sizeof(s.", fn, "));");
    }
    if (ft == "bool") {
        return cat("    out.append((char*)&s.", fn, ", 1);");
    }
    return "";
}

auto unpack_field(const std::string& ft, const std::string& fn) -> std::string {
    if (is_str_type(ft)) {
        return cat("    if (p + 4 <= end) { uint32_t len; memcpy(&len, p, 4); p += 4; if (p + len <= end) { out.", fn,
                   " = std::string(p, len); p += len; } }");
    }
    if (is_num_type(ft)) {
        return cat("    if (p + sizeof(out.", fn, ") <= end) { memcpy(&out.", fn, ", p, sizeof(out.", fn,
                   ")); p += sizeof(out.", fn, "); }");
    }
    if (ft == "bool") {
        return cat("    if (p + 1 <= end) { memcpy(&out.", fn, ", p, 1); p += 1; }");
    }
    return "";
}

struct Field {
    std::string type;
    std::string name;
};

// Strips a trailing `= ...` initializer from a single declaration.
auto drop_default(const std::string& line) -> std::string {
    const size_t eq = line.find('=');
    if (eq == std::string::npos || line.find('\n', eq) != std::string::npos) {
        return line;
    }
    const size_t last = eq == 0 ? std::string::npos : line.find_last_not_of(" \t\n\r\f\v", eq - 1);
    return line.substr(0, last == std::string::npos ? 0 : last + 1);
}

auto struct_fields(const std::string& body) -> std::vector<Field> {
    // One declaration per ';'.
    auto field_of = [](auto piece) -> std::optional<Field> {
        std::string line = drop_default(strip(std::string_view(piece)));
        if (const auto m = ctre::search<R"RE(([\w:<>]+)\s+(\w+)$)RE">(line)) {
            return Field{.type = m.get<1>().to_string(), .name = m.get<2>().to_string()};
        }
        return std::nullopt;
    };
    return body | std::views::split(';') | std::views::transform(field_of) |
           std::views::filter([](const std::optional<Field>& f) -> bool { return f.has_value(); }) |
           std::views::transform([](const std::optional<Field>& f) -> Field { return *f; }) |
           std::ranges::to<std::vector<Field>>();
}

} // namespace

auto gen_states(const Ctx& ctx) -> int {
    const fs::path out_path = setup(ctx, "states.hpp");
    const fs::path site = ctx.proj / "app" / "routes";
    const fs::path layouts = ctx.proj / "app" / "layouts";
    const fs::path models_dir = ctx.proj / "models";
    const fs::path models_site = site / "models.hpp";

    std::vector<fs::path> files = glob(site, ".cpp", true);
    std::ranges::copy(glob(layouts, ".cpp", true), std::back_inserter(files));

    std::set<std::string> types;
    const auto add_types = [&](const Lines& found) -> void { types.insert(found.begin(), found.end()); };
    for (const auto& f : files) {
        const std::string content = read_text(f);
        // web::State<T> / web::Flash<T> arguments, ctx.state(T{...}) / ctx.flash(T{...}) writes, has<T> / clear<T>.
        add_types(captures<R"RE(web::(State|Flash)<(\w+)>)RE", 2>(content));
        add_types(captures<R"RE(\.(?:state|flash)\((\w+)\s*\{)RE">(content));
        add_types(captures<R"RE((?:has|clear)<(\w+)>)RE">(content));
    }

    if (types.empty()) {
        write_file(out_path, "#pragma once\n\nnamespace web::states {\n    template<typename T> struct Traits;\n"
                             "    template<typename T> std::string pack(const T& val);\n}\n");
        return 0;
    }

    Lines hpp = {
        "// Auto-generated — do not edit", "#pragma once", "#include <revet/web.hpp>", "#include <revet/sanitize.hpp>",
        "#include <revet/crypto.hpp>",
    };
    if (has_models(models_dir)) {
        hpp.emplace_back("#include \"models.hpp\"");
    }
    if (file_exists(models_site)) {
        hpp.push_back(rel_include(models_site, ctx.proj));
    }
    append(hpp, {"#include <string>", "#include <cstring>", "", "namespace web::states {", "",
                 "template<typename T> struct Traits;", "template<typename T> std::string pack(const T& val);",
                 "template<typename T> T unpack(std::string_view);",
                 "template<typename T> std::string pack_plain(const T& val);",
                 "template<typename T> T unpack_plain(std::string_view);", ""});

    // First definition wins; files are searched in order, then model headers, then models_site.
    std::vector<fs::path> search = files;
    std::ranges::copy(glob(models_dir, ".hpp"), std::back_inserter(search));
    if (file_exists(models_site)) {
        search.push_back(models_site);
    }

    std::map<std::string, std::vector<Field>> struct_defs;
    for (const auto& f : search) {
        const std::string content = read_text(f);
        // `struct T { fields };` (optionally `struct [[attr]] T`). First definition of each type wins.
        const std::string_view view = content;
        for (const auto head : ctre::search_all<R"RE((?:struct|class)\s+(?:\[\[[^\n]*?\]\]\s*)?(\w+)\s*\{)RE">(view)) {
            const std::string name = head.get<1>().to_string();
            if ((!types.contains(name)) || struct_defs.contains(name)) {
                continue;
            }
            const std::string_view rest = view.substr(offset_in(view, head.to_view()) + head.size());
            if (const auto body = ctre::starts_with<R"RE(([^}]+)\};)RE">(rest)) {
                struct_defs[name] = struct_fields(body.get<1>().to_string());
            }
        }
    }

    const std::string traits_t = tmpl(ctx, "states_traits.tmpl");
    const std::string pack_enc_t = tmpl(ctx, "states_pack_enc.tmpl");
    const std::string unpack_enc_t = tmpl(ctx, "states_unpack_enc.tmpl");
    const std::string specializations_t = tmpl(ctx, "states_specializations.tmpl");
    const std::string cookie_helper_t = tmpl(ctx, "states_cookie_helper.tmpl");
    const std::string pack_plain_fn_t = tmpl(ctx, "states_pack_plain_fn.tmpl");
    const std::string unpack_plain_fn_t = tmpl(ctx, "states_unpack_plain_fn.tmpl");

    for (const auto& t : types) {
        const auto it = struct_defs.find(t);
        if (it == struct_defs.end()) {
            throw GenError("gen_states: definition for '" + t + "' not found in any scanned header or source file");
        }
        const auto& fields = it->second;
        hpp.push_back(subst(traits_t, {{"t", t}}));
        hpp.emplace_back("");

        // Fields of unsupported types are left out.
        const auto field_lines = [&](auto codec) -> std::string {
            return join(fields | std::views::transform([&](const Field& fd) -> std::string {
                            return codec(fd.type, fd.name);
                        }) | std::views::filter([](const std::string& ln) -> bool { return !ln.empty(); }) |
                            std::ranges::to<Lines>(),
                        "\n");
        };
        hpp.push_back(subst(pack_plain_fn_t, {{"t", t}, {"body", field_lines(pack_field)}}));
        hpp.emplace_back("");

        hpp.push_back(subst(pack_enc_t, {{"t", t}}));
        hpp.emplace_back("");

        hpp.push_back(subst(unpack_plain_fn_t, {{"t", t}, {"body", field_lines(unpack_field)}}));
        hpp.emplace_back("");

        hpp.push_back(subst(unpack_enc_t, {{"t", t}}));
        hpp.emplace_back("");

        hpp.push_back(subst(specializations_t, {{"t", t}}));
        hpp.emplace_back("");
    }

    hpp.emplace_back("");
    hpp.emplace_back("// Type-safe cookie helpers");
    std::ranges::transform(types, std::back_inserter(hpp),
                           [&](const std::string& t) -> std::string { return subst(cookie_helper_t, {{"t", t}}); });

    hpp.emplace_back("");
    hpp.emplace_back("// has<T>: check if a state cookie is present (for use in route_guards before state injection)");
    hpp.emplace_back("template<typename T>");
    hpp.emplace_back("[[nodiscard]] inline bool has(const web::ParamMap& cookies) {");
    hpp.emplace_back("    return !cookies.get(std::string(cookie_prefix).append(Traits<T>::name)).empty();");
    hpp.emplace_back("}");
    hpp.emplace_back("} // namespace web::states");
    write_lines(out_path, hpp);
    return 0;
}

} // namespace revet::codegen
