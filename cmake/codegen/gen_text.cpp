// CSS class/id constants and locale strings.
#include <cctype>
#include <iostream>
#include <map>
#include <set>

#include "common.hpp"

namespace revet::codegen {
namespace {

// CSS name -> C++ identifier: dashes become underscores, and a C++ keyword or
// alternative token (`or`, `not`, `float`, `default`...) gets a trailing underscore.
auto replace_dashes(std::string s) -> std::string {
    static const std::set<std::string, std::less<>> keywords{
        "alignas",     "alignof",   "and",        "and_eq",    "asm",      "auto",         "bitand",
        "bitor",       "bool",      "break",      "case",      "catch",    "char",         "char8_t",
        "char16_t",    "char32_t",  "class",      "compl",     "concept",  "const",        "consteval",
        "constexpr",   "constinit", "const_cast", "continue",  "co_await", "co_return",    "co_yield",
        "decltype",    "default",   "delete",     "do",        "double",   "dynamic_cast", "else",
        "enum",        "explicit",  "export",     "extern",    "false",    "float",        "for",
        "friend",      "goto",      "if",         "inline",    "int",      "long",         "mutable",
        "namespace",   "new",       "noexcept",   "not",       "not_eq",   "nullptr",      "operator",
        "or",          "or_eq",     "private",    "protected", "public",   "register",     "reinterpret_cast",
        "requires",    "return",    "short",      "signed",    "sizeof",   "static",       "static_assert",
        "static_cast", "struct",    "switch",     "template",  "this",     "thread_local", "throw",
        "true",        "try",       "typedef",    "typeid",    "typename", "union",        "unsigned",
        "using",       "virtual",   "void",       "volatile",  "wchar_t",  "while",        "xor",
        "xor_eq"};
    for (char& c : s) {
        if (c == '-') {
            c = '_';
        }
    }
    if (keywords.contains(s)) {
        s += '_';
    }
    return s;
}

// ── CSV (RFC 4180 subset: quoted fields, doubled quotes, embedded newlines) ──

auto parse_csv(const std::string& text) -> std::vector<std::vector<std::string>> {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool in_quotes = false;
    bool row_has_data = false;
    auto end_field = [&] -> void {
        row.push_back(field);
        field.clear();
    };
    auto end_row = [&] -> void {
        if (row_has_data) {
            rows.push_back(row);
        }
        row.clear();
        row_has_data = false;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"') {
            in_quotes = true;
            row_has_data = true;
        } else if (c == ',') {
            end_field();
            row_has_data = true;
        } else if (c == '\n') {
            end_field();
            end_row();
        } else {
            field += c;
            row_has_data = true;
        }
    }
    if (row_has_data || !field.empty()) {
        end_field();
        end_row();
    }
    return rows;
}

auto iso_639_1() -> const std::set<std::string>& {
    static const std::set<std::string> codes = {
        "af", "sq", "am", "ar", "hy", "az", "eu", "be", "bn", "bs", "bg", "ca", "zh", "co", "hr", "cs", "da", "nl",
        "en", "eo", "et", "fi", "fr", "fy", "gl", "ka", "de", "el", "gu", "ht", "ha", "he", "hi", "hu", "is", "ig",
        "id", "ga", "it", "ja", "jv", "kn", "kk", "km", "ko", "ku", "ky", "lo", "la", "lv", "lt", "lb", "mk", "mg",
        "ms", "ml", "mt", "mi", "mr", "mn", "my", "ne", "no", "ny", "or", "ps", "fa", "pl", "pt", "pa", "ro", "ru",
        "sm", "gd", "sr", "st", "sn", "sd", "si", "sk", "sl", "so", "es", "su", "sw", "sv", "tl", "tg", "ta", "tt",
        "te", "th", "tr", "tk", "uk", "ur", "ug", "uz", "vi", "cy", "xh", "yi", "yo", "zu",
    };
    return codes;
}

} // namespace

auto gen_styles(const Ctx& ctx) -> int {
    fs::path out_path = setup(ctx, "styles.hpp");
    fs::path styles_dir = ctx.proj / "app" / "styles";
    if (!file_exists(styles_dir)) {
        throw GenError("gen_styles: styles directory not found: " + styles_dir.string());
    }

    const std::string named_class_t = tmpl(ctx, "styles_named_class.tmpl");
    const std::string id_t = tmpl(ctx, "styles_id.tmpl");

    Lines out = file_header({"#include <revet/css.hpp>"});
    out.emplace_back("namespace styles {");
    out.emplace_back("");

    std::vector<std::string> all_css;
    std::set<std::string> defined_classes;
    std::map<std::string, std::string> defined_ids; // cpp name → css name

    for (const auto& css_file : glob(styles_dir, ".css")) {
        std::string content = read_text(css_file);
        all_css.push_back(content);

        // Selectors on lines that end with a comma carry over to the block's '{' line.
        std::vector<std::pair<size_t, std::string>> pending;
        size_t lineno = 0;
        for (const auto& line : splitlines(content)) {
            ++lineno;
            std::string stripped = strip(line);
            if (stripped.empty()) {
                pending.clear();
                continue;
            }
            bool has_open = stripped.contains('{');
            bool ends_comma = stripped.back() == ',';
            if (!has_open && !ends_comma) {
                pending.clear();
                continue;
            }

            auto candidates = pending;
            candidates.emplace_back(lineno, stripped);
            if (has_open) {
                for (const auto& [li, sel] : candidates) {
                    for (auto m : ctre::search_all<R"RE(\.([A-Za-z][A-Za-z0-9_\-]*))RE">(std::string_view(sel))) {
                        std::string css_name = m.get<1>().to_string();
                        std::string cpp_name = replace_dashes(css_name);
                        if (!defined_classes.contains(cpp_name)) {
                            out.push_back(subst(named_class_t, {{"line", std::to_string(li)},
                                                                {"file", css_file.string()},
                                                                {"cpp_name", cpp_name},
                                                                {"css_name", css_name}}));
                            defined_classes.insert(cpp_name);
                        }
                    }
                    for (auto m : ctre::search_all<R"RE(#([A-Za-z][A-Za-z0-9_\-]*))RE">(std::string_view(sel))) {
                        std::string css_name = m.get<1>().to_string();
                        defined_ids.emplace(replace_dashes(css_name), css_name);
                    }
                }
                pending.clear();
            } else {
                pending = candidates;
            }
        }
    }

    out.emplace_back("} // namespace styles");
    out.emplace_back("");

    if (!defined_ids.empty()) {
        out.emplace_back("namespace ids {");
        out.emplace_back("");
        for (const auto& [cpp_name, css_name] : defined_ids) {
            out.push_back(subst(id_t, {{"cpp_name", cpp_name}, {"css_name", css_name}}));
        }
        out.emplace_back("");
        out.emplace_back("} // namespace ids");
    }
    out.emplace_back("");
    out.emplace_back("namespace html::detail {");
    std::string joined;
    for (size_t i = 0; i < all_css.size(); ++i) {
        if (i != 0U) {
            joined += '\n';
        }
        joined += all_css[i];
    }
    out.push_back(cat("inline const std::string _all_css_str = \"", escape_str(joined), "\";"));
    // Emitted verbatim.
    out.emplace_back("inline const bool _all_css_init = ([]{{ all_css_block = _all_css_str; return true; }})();");
    out.emplace_back("} // namespace html::detail");
    out.emplace_back("");

    write_lines(out_path, out);
    return 0;
}

auto gen_i18n(const Ctx& ctx) -> int {
    fs::path out_path = setup(ctx, "i18n.hpp");
    fs::path csv_path = ctx.proj / "app" / "locale" / "strings.csv";

    if (!file_exists(csv_path)) {
        write_file(out_path, "#pragma once\n\nnamespace i18n {}\n");
        return 0;
    }

    auto rows = parse_csv(read_text(csv_path));
    if (rows.empty()) {
        throw GenError("gen_i18n: strings.csv has no header");
    }
    const auto& header = rows.front();

    std::vector<std::string> locales;
    for (const auto& h : header) {
        if (h != "key") {
            locales.push_back(h);
        }
    }

    std::vector<std::map<std::string, std::string>> records;
    for (size_t r = 1; r < rows.size(); ++r) {
        std::map<std::string, std::string> rec;
        for (size_t c = 0; c < header.size(); ++c) {
            if (c < rows[r].size()) {
                rec[header[c]] = rows[r][c];
            }
        }
        records.push_back(std::move(rec));
    }

    Lines bad;
    for (const auto& l : locales) {
        if (static_cast<unsigned int>(iso_639_1().contains(l)) == 0U) {
            bad.push_back(l);
        }
    }
    if (!bad.empty()) {
        throw GenError("ERROR gen_i18n: unrecognized locale code(s): " + join(bad, ", ") +
                       "\n  Locale column headers must be ISO 639-1 two-letter codes (e.g. en, es, fr, de).");
    }

    const size_t n = locales.size();
    std::string picker;
    for (size_t i = 0; i < locales.size(); ++i) {
        const std::string& l = locales[i];
        std::string upper = l;
        for (char& c : upper) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (i != 0U) {
            picker += "\n        ";
        }
        picker += cat(R"(html::button{html::attr.type("submit"), html::attr.name("lang"), html::attr.value(")", l,
                      "\"), styles::btn, detail::locale_idx == ", std::to_string(i),
                      " ? html::Node(styles::btn_solid) : html::Node(styles::btn_ghost), \"", upper, "\"},");
    }

    const std::string str_const_t = tmpl(ctx, "i18n_str_const.tmpl");
    std::string str_consts;
    for (size_t r = 0; r < records.size(); ++r) {
        const auto& rec = records[r];
        auto key_it = rec.find("key");
        if (key_it == rec.end()) {
            throw GenError("gen_i18n: row without a 'key' column");
        }
        std::string translations;
        for (size_t i = 0; i < locales.size(); ++i) {
            auto v = rec.find(locales[i]);
            if (v == rec.end()) {
                throw GenError("gen_i18n: missing translation for " + locales[i] + " in " + key_it->second);
            }
            if (i != 0U) {
                translations += ", ";
            }
            translations += "\"" + escape_str(v->second) + "\"";
        }
        if (r != 0U) {
            str_consts += '\n';
        }
        str_consts +=
            subst(str_const_t, {{"n", std::to_string(n)}, {"key", key_it->second}, {"translations", translations}});
    }

    std::string quoted_locales;
    for (size_t i = 0; i < locales.size(); ++i) {
        if (i != 0U) {
            quoted_locales += ", ";
        }
        quoted_locales += "\"" + locales[i] + "\"";
    }

    const std::string header_t = tmpl_whole(ctx, "i18n_header.tmpl");
    write_file(out_path, subst(header_t, {{"locale_enum", join(locales, ", ")},
                                          {"locale_names_arr", quoted_locales},
                                          {"n", std::to_string(n)},
                                          {"picker_buttons", picker},
                                          {"str_consts", str_consts}}));
    return 0;
}

} // namespace revet::codegen
