// Reads top-level revet.toml settings (max_body, data_dir) and writes build/generated/server.hpp.
// The header sets them during static initialisation, so REVET_MAX_BODY and --max-body
// (read later by web::init) still win.
#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace revet::codegen {
namespace {

const char* kHeader = "// Auto-generated from revet.toml — do not edit\n#pragma once\n#include <revet/web.hpp>\n";

// "64MB", "512KiB", "1g" or a plain integer → bytes (binary units).
std::size_t to_bytes(const std::string& raw_value, bool is_string) {
    if (!is_string) {
        if (raw_value == "true" || raw_value == "false") throw GenError("revet.toml: max_body must be a size, not a boolean");
        std::string digits;
        for (char c : raw_value) {
            if (c != '_') digits.push_back(c);
        }
        if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
            throw GenError("revet.toml: max_body = \"" + raw_value + "\" is not a size (try \"64MB\")");
        }
        std::size_t n = std::stoull(digits);
        if (n < 1) throw GenError("revet.toml: max_body must be at least 1 byte");
        return n;
    }
    static const Re size_re(R"RE(\s*(\d+(?:\.\d+)?)\s*([kmgKMG]?[iI]?[bB]?)\s*)RE");
    Match m;
    if (!match_full(raw_value, m, size_re)) {
        throw GenError("revet.toml: max_body = \"" + raw_value + "\" is not a size (try \"64MB\")");
    }
    // Same unit handling as the Python generator: lower, strip trailing "b", then "i".
    std::string unit = lower(m[2].str());
    while (!unit.empty() && unit.back() == 'b') unit.pop_back();
    while (!unit.empty() && unit.back() == 'i') unit.pop_back();
    double mult = 1;
    if (unit == "k") mult = 1024.0;
    else if (unit == "m") mult = 1024.0 * 1024.0;
    else if (unit == "g") mult = 1024.0 * 1024.0 * 1024.0;
    else if (!unit.empty()) throw GenError("revet.toml: max_body = \"" + raw_value + "\" is not a size (try \"64MB\")");
    auto n = static_cast<std::size_t>(std::stod(m[1].str()) * mult);
    if (n < 1) throw GenError("revet.toml: max_body must be at least 1 byte");
    return n;
}

// TOML basic-string escapes, so data_dir sees the same text tomllib would give.
std::string unescape_basic(const std::string& in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) {
            out.push_back(in[i]);
            continue;
        }
        char n = in[++i];
        switch (n) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            default: out.push_back(n); break;  // \\ and \" and anything else: the char itself
        }
    }
    return out;
}

struct TopLevel {
    std::optional<std::string> max_body;  // raw value text
    bool max_body_is_string = false;
    std::optional<std::string> data_dir;
};

// Top-level keys only: anything after the first [table] header is ignored.
TopLevel read_top_level(const fs::path& toml) {
    TopLevel out;
    if (!file_exists(toml)) return out;
    static const Re key_re(R"RE(\s*(max_body|data_dir)\s*=\s*(.*))RE");
    static const Re quoted_re(R"RE("((?:[^"\\]|\\.)*)"\s*(?:#.*)?)RE");
    static const Re bare_re(R"RE(([^#\s]+)\s*(?:#.*)?)RE");
    for (const auto& line : splitlines(read_text(toml))) {
        std::string s = strip(line);
        if (starts_with(s, "[")) break;
        Match m;
        if (!match_prefix(s, m, key_re)) continue;
        std::string key = m[1].str();
        std::string rest = m[2].str();
        Match v;
        bool is_string = !rest.empty() && rest[0] == '"';
        std::string value;
        if (is_string) {
            if (!match_prefix(rest, v, quoted_re)) continue;
            value = unescape_basic(v[1].str());
        } else {
            if (!match_prefix(rest, v, bare_re)) continue;
            value = v[1].str();
        }
        if (key == "max_body") {
            out.max_body = value;
            out.max_body_is_string = is_string;
        } else if (is_string) {
            out.data_dir = value;
        }
    }
    return out;
}

}  // namespace

int gen_server(const Ctx& ctx) {
    fs::path out_path = setup(ctx, "server.hpp");
    TopLevel top = read_top_level(ctx.proj / "revet.toml");

    std::vector<std::string> body;
    if (!top.max_body) {
        body.push_back("// max_body not set: web::config::DEFAULT_MAX_BODY_BYTES applies");
    } else {
        std::size_t n = to_bytes(*top.max_body, top.max_body_is_string);
        body.push_back("inline const bool max_body_set = (web::config::max_body_bytes(" + std::to_string(n) + "UL), true);");
    }
    if (top.data_dir) {
        std::string escaped;
        for (char c : *top.data_dir) {
            if (c == '\\' || c == '"') escaped.push_back('\\');
            escaped.push_back(c);
        }
        body.push_back("inline const bool data_dir_set = (web::config::data_dir(\"" + escaped + "\"), true);");
    }

    std::string text = kHeader;
    text += "namespace revet::generated {\n";
    text += join(body, "\n");
    text += "\n} // namespace revet::generated\n";
    write_file(out_path, text);
    return 0;
}

}  // namespace revet::codegen
