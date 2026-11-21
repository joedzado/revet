// revet.toml top-level settings (max_body, data_dir) -> server.hpp.
// The header sets them during static initialisation, so REVET_MAX_BODY and --max-body
// (read later by web::init) still win.
#include <optional>

#include "common.hpp"
#include "node.hpp"

namespace revet::codegen {
namespace {

[[noreturn]] void bad_size(const std::string& shown) {
    throw GenError("revet.toml: max_body = \"" + shown + "\" is not a size (try \"64MB\")");
}

// Integer bytes, or a string like "64MB" / "512KiB" / "1g". Binary units.
unsigned long long to_bytes(const Node& value) {
    if (value.kind == Node::Kind::Bool) {
        throw GenError("revet.toml: max_body must be a size, not a boolean");
    }

    double n = 0;
    if (value.kind == Node::Kind::Number && ctre::match<R"RE([+\-]?\d+)RE">(value.s)) {
        n = static_cast<double>(py_int(value));
    } else {
        const std::string text = py_str(value);
        auto m = ctre::match<R"RE(\s*(\d+(?:\.\d+)?)\s*([kKmMgG]?)[iI]?[bB]?\s*)RE">(text);
        if (!m) {
            bad_size(text);
        }
        double unit = 1;
        switch (m.get<2>().to_view().empty() ? '\0' : lower(m.get<2>().to_view())[0]) {
            case 'k':
                unit = 1024.0;
                break;
            case 'm':
                unit = 1024.0 * 1024.0;
                break;
            case 'g':
                unit = 1024.0 * 1024.0 * 1024.0;
                break;
            default:
                break;
        }
        n = std::stod(m.get<1>().to_string()) * unit;
    }
    if (n < 1) {
        throw GenError("revet.toml: max_body must be at least 1 byte");
    }
    return static_cast<unsigned long long>(n);
}

} // namespace

int gen_server(const Ctx& ctx) {
    fs::path out_path = setup(ctx, "server.hpp");
    fs::path toml = ctx.proj / "revet.toml";

    std::optional<unsigned long long> max_body;
    std::optional<std::string> data_dir;
    if (file_exists(toml)) {
        Node root = parse_toml(read_text(toml));
        if (const Node* v = root.find("max_body")) {
            max_body = to_bytes(*v);
        }
        if (const Node* v = root.find("data_dir"); v != nullptr && v->kind == Node::Kind::String) {
            data_dir = v->s;
        }
    }

    Lines body;
    if (max_body) {
        body.push_back(cat("inline const bool max_body_set = (web::config::max_body_bytes(", std::to_string(*max_body),
                           "UL), true);"));
    } else {
        body.push_back("// max_body not set: web::config::DEFAULT_MAX_BODY_BYTES applies");
    }
    if (data_dir) {
        std::string escaped;
        for (char c : *data_dir) {
            if (c == '\\' || c == '"') {
                escaped += '\\';
            }
            escaped += c;
        }
        body.push_back(cat("inline const bool data_dir_set = (web::config::data_dir(\"", escaped, "\"), true);"));
    }

    write_file(out_path,
               cat("// Auto-generated from revet.toml — do not edit\n#pragma once\n#include <revet/web.hpp>\n",
                   "namespace revet::generated {\n", join(body, "\n"), "\n} // namespace revet::generated\n"));
    return 0;
}

} // namespace revet::codegen
