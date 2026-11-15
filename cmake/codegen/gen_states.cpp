// web::State / web::Flash / toggle types -> states.hpp (cookie codecs).
#include "common.hpp"

#include <cctype>
#include <map>
#include <set>

namespace revet::codegen {
namespace {

bool is_str_type(const std::string& t) { return t == "std::string" || t == "std::string_view"; }

bool is_num_type(const std::string& t) {
    static const std::set<std::string> nums{"int",      "int32_t", "int64_t", "int16_t", "uint32_t", "uint64_t",
                                            "uint16_t", "size_t",  "float",   "double"};
    return nums.count(t) != 0;
}

// Returns "" for unsupported types.
std::string pack_field(const std::string& ft, const std::string& fn) {
    if (is_str_type(ft)) {
        return cat("    { uint32_t len = (uint32_t)s.", fn, ".size(); out.append((char*)&len, 4); out.append(s.", fn,
                   "); }");
    }
    if (is_num_type(ft)) return cat("    out.append((char*)&s.", fn, ", sizeof(s.", fn, "));");
    if (ft == "bool") return cat("    out.append((char*)&s.", fn, ", 1);");
    return "";
}

std::string unpack_field(const std::string& ft, const std::string& fn) {
    if (is_str_type(ft)) {
        return cat("    if (p + 4 <= end) { uint32_t len; memcpy(&len, p, 4); p += 4; if (p + len <= end) { out.", fn,
                   " = std::string(p, len); p += len; } }");
    }
    if (is_num_type(ft)) {
        return cat("    if (p + sizeof(out.", fn, ") <= end) { memcpy(&out.", fn, ", p, sizeof(out.", fn,
                   ")); p += sizeof(out.", fn, "); }");
    }
    if (ft == "bool") return cat("    if (p + 1 <= end) { memcpy(&out.", fn, ", p, 1); p += 1; }");
    return "";
}

struct Field {
    std::string type;
    std::string name;
};

// Strips a trailing `= ...` initializer from a single declaration.
std::string drop_default(const std::string& line) {
    size_t eq = line.find('=');
    if (eq == std::string::npos || line.find('\n', eq) != std::string::npos) return line;
    size_t cut = eq;
    while (cut > 0 && std::isspace(static_cast<unsigned char>(line[cut - 1]))) --cut;
    return line.substr(0, cut);
}

std::vector<Field> struct_fields(const std::string& body) {
    std::vector<Field> fields;
    std::string piece;
    auto handle = [&](const std::string& raw) {
        std::string line = strip(raw);
        if (line.empty()) return;
        line = drop_default(line);
        if (auto m = ctre::search<R"RE(([\w:<>]+)\s+(\w+)$)RE">(line)) {
            fields.push_back({m.get<1>().to_string(), m.get<2>().to_string()});
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

}  // namespace

int gen_states(const Ctx& ctx) {
    fs::path out_path = setup(ctx, "states.hpp");
    fs::path site = ctx.proj / "app" / "routes";
    fs::path layouts = ctx.proj / "app" / "layouts";
    fs::path models_dir = ctx.proj / "models";
    fs::path models_site = site / "models.hpp";

    std::vector<fs::path> files = glob(site, ".cpp", true);
    for (auto& f : glob(layouts, ".cpp", true)) files.push_back(f);

    std::set<std::string> types;
    for (const auto& f : files) {
        std::string content = read_text(f);
        const std::string_view view = content;
        // web::State<T> / web::Flash<T> arguments, ctx.state(T{...}) / ctx.flash(T{...}) writes, has<T> / clear<T>.
        for (auto m : find_state_types(view)) types.insert(m.get<2>().to_string());
        for (auto m : ctre::search_all<R"RE(\.(?:state|flash)\((\w+)\s*\{)RE">(view)) types.insert(m.get<1>().to_string());
        for (auto m : ctre::search_all<R"RE((?:has|clear)<(\w+)>)RE">(view)) types.insert(m.get<1>().to_string());
    }

    if (types.empty()) {
        write_file(out_path,
                   "#pragma once\n\nnamespace web::states {\n    template<typename T> struct Traits;\n"
                   "    template<typename T> std::string pack(const T& val);\n}\n");
        return 0;
    }

    Lines hpp = {
        "// Auto-generated — do not edit",
        "#pragma once",
        "#include <revet/web.hpp>",
        "#include <revet/sanitize.hpp>",
        "#include <revet/crypto.hpp>",
    };
    if (has_models(models_dir)) hpp.push_back("#include \"models.hpp\"");
    if (file_exists(models_site)) hpp.push_back(rel_include(models_site, ctx.proj));
    for (const auto& line : Lines{
             "#include <string>",
             "#include <cstring>",
             "",
             "namespace web::states {",
             "",
             "template<typename T> struct Traits;",
             "template<typename T> std::string pack(const T& val);",
             "template<typename T> T unpack(std::string_view);",
             "template<typename T> std::string pack_plain(const T& val);",
             "template<typename T> T unpack_plain(std::string_view);",
             ""}) {
        hpp.push_back(line);
    }

    // First definition wins; files are searched in order, then model headers, then models_site.
    std::vector<fs::path> search = files;
    for (auto& f : glob(models_dir, ".hpp")) search.push_back(f);
    if (file_exists(models_site)) search.push_back(models_site);

    std::map<std::string, std::vector<Field>> struct_defs;
    for (const auto& f : search) {
        std::string content = read_text(f);
        // `struct T { fields };` (optionally `struct [[attr]] T`). First definition of each type wins.
        const std::string_view view = content;
        for (auto head : ctre::search_all<R"RE((?:struct|class)\s+(?:\[\[[^\n]*?\]\]\s*)?(\w+)\s*\{)RE">(view)) {
            std::string name = head.get<1>().to_string();
            if (!types.count(name) || struct_defs.count(name)) continue;
            std::string_view rest = view.substr(offset_in(view, head.to_view()) + head.size());
            if (auto body = ctre::starts_with<R"RE(([^}]+)\};)RE">(rest)) {
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
        auto it = struct_defs.find(t);
        if (it == struct_defs.end()) {
            throw GenError("gen_states: definition for '" + t + "' not found in any scanned header or source file");
        }
        const auto& fields = it->second;
        hpp.push_back(subst(traits_t, {{"t", t}}));
        hpp.push_back("");

        Lines pack_lines;
        for (const auto& fd : fields) {
            std::string ln = pack_field(fd.type, fd.name);
            if (!ln.empty()) pack_lines.push_back(ln);
        }
        hpp.push_back(subst(pack_plain_fn_t, {{"t", t}, {"body", join(pack_lines, "\n")}}));
        hpp.push_back("");

        hpp.push_back(subst(pack_enc_t, {{"t", t}}));
        hpp.push_back("");

        Lines unpack_lines;
        for (const auto& fd : fields) {
            std::string ln = unpack_field(fd.type, fd.name);
            if (!ln.empty()) unpack_lines.push_back(ln);
        }
        hpp.push_back(subst(unpack_plain_fn_t, {{"t", t}, {"body", join(unpack_lines, "\n")}}));
        hpp.push_back("");

        hpp.push_back(subst(unpack_enc_t, {{"t", t}}));
        hpp.push_back("");

        hpp.push_back(subst(specializations_t, {{"t", t}}));
        hpp.push_back("");
    }

    hpp.push_back("");
    hpp.push_back("// Type-safe cookie helpers");
    for (const auto& t : types) hpp.push_back(subst(cookie_helper_t, {{"t", t}}));

    hpp.push_back("");
    hpp.push_back("// has<T>: check if a state cookie is present (for use in route_guards before state injection)");
    hpp.push_back("template<typename T>");
    hpp.push_back("[[nodiscard]] inline bool has(const web::ParamMap& cookies) {");
    hpp.push_back("    return !cookies.get(std::string(cookie_prefix).append(Traits<T>::name)).empty();");
    hpp.push_back("}");
    hpp.push_back("} // namespace web::states");
    write_lines(out_path, hpp);
    return 0;
}

}  // namespace revet::codegen
