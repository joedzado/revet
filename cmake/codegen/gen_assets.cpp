// Embeds bundled JS (htmx, optimistic) as string_view constants.
// Both are invoked from gen_routes (routes.cpp includes their headers).
#include "common.hpp"

#include <chrono>
#include <cstdlib>

namespace revet::codegen {
namespace {

std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

fs::path find_bun() {
    if (const char* path = std::getenv("PATH")) {
        std::string p = path;
        size_t start = 0;
        while (start <= p.size()) {
            size_t end = p.find(':', start);
            if (end == std::string::npos) end = p.size();
            fs::path candidate = fs::path(p.substr(start, end - start)) / "bun";
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) return candidate;
            start = end + 1;
        }
    }
    if (const char* home = std::getenv("HOME")) return fs::path(home) / ".bun" / "bin" / "bun";
    return {};
}

}  // namespace

int gen_htmx(const Ctx& ctx) {
    fs::path out_path = setup(ctx, "htmx_asset.hpp");

    std::string version = "2.0.9";
    fs::path toml = ctx.proj / "revet.toml";
    if (file_exists(toml)) {
        static const Re section_re(R"RE(\s*\[htmx\])RE");
        static const Re version_re(R"RE(\s*version\s*=\s*"([^"]+)")RE");
        bool in_htmx = false;
        for (const auto& line : splitlines(read_text(toml))) {
            Match m;
            if (match_prefix(line, m, section_re)) {
                in_htmx = true;
                continue;
            }
            if (in_htmx) {
                if (starts_with(strip(line), "[")) break;
                if (match_prefix(line, m, version_re)) {
                    version = m[1].str();
                    break;
                }
            }
        }
    }

    fs::path vendor_file = ctx.framework / "vendor" / "htmx" / version / "htmx.min.js";
    if (!file_exists(vendor_file)) {
        throw GenError("gen_htmx: htmx " + version + " not vendored — run: scripts/vendor_htmx.sh");
    }

    Lines out = file_header({"#include <string_view>"});
    out.push_back("namespace revet::detail {");
    out.push_back(cat("inline constexpr std::string_view htmx_version = \"", version, "\";"));
    out.push_back(cat("inline constexpr std::string_view htmx_js = \"", escape_str(read_text(vendor_file)), "\";"));
    out.push_back("} // namespace revet::detail");
    out.push_back("");
    write_lines(out_path, out);
    return 0;
}

int gen_optimistic(const Ctx& ctx) {
    fs::path out_path = setup(ctx, "optimistic_asset.hpp");
    fs::path ts_file = ctx.framework / "static" / "optimistic.ts";
    if (!file_exists(ts_file)) throw GenError("gen_optimistic: " + ts_file.string() + " not found");

    fs::path bun = find_bun();
    if (bun.empty() || !file_exists(bun)) throw GenError("gen_optimistic: bun not found");

    fs::path tmp_dir = fs::temp_directory_path() / ("revet-codegen-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp_dir);
    fs::path js_tmp = tmp_dir / "optimistic.js";
    fs::path err_tmp = tmp_dir / "bun.err";

    std::string cmd = cat(shell_quote(bun.string()), " build --minify ", shell_quote(ts_file.string()),
                          " --outfile ", shell_quote(js_tmp.string()), " 2>", shell_quote(err_tmp.string()));
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::string err = file_exists(err_tmp) ? read_text(err_tmp) : "";
        fs::remove_all(tmp_dir);
        throw GenError("gen_optimistic: bun build failed:\n" + err);
    }

    std::string js = read_text(js_tmp);
    fs::remove_all(tmp_dir);

    Lines out = file_header({"#include <string_view>"});
    out.push_back("namespace revet::detail {");
    out.push_back(cat("inline constexpr std::string_view optimistic_js = \"", escape_str(js), "\";"));
    out.push_back("} // namespace revet::detail");
    out.push_back("");
    write_lines(out_path, out);
    return 0;
}

}  // namespace revet::codegen
