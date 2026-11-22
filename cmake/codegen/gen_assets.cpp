// Embeds bundled JS (htmx, optimistic) as string_view constants.
// Both are invoked from gen_routes (routes.cpp includes their headers).
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ranges>

#include "common.hpp"

namespace revet::codegen {
namespace {

auto shell_quote(const std::string& s) -> std::string {
    return "'" + replace_all<"'">(s, "'\\''") + "'";
}

auto find_bun() -> fs::path {
    if (const char* path = std::getenv("PATH")) {
        auto candidates =
            std::string_view(path) | std::views::split(':') |
            std::views::transform([](auto dir) -> fs::path { return fs::path(std::string_view(dir)) / "bun"; });
        auto found = std::ranges::find_if(candidates, [](const fs::path& p) -> bool {
            std::error_code ec;
            return fs::is_regular_file(p, ec);
        });
        if (found != candidates.end()) {
            return *found;
        }
    }
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / ".bun" / "bin" / "bun";
    }
    return {};
}

} // namespace

auto gen_htmx(const Ctx& ctx) -> int {
    fs::path out_path = setup(ctx, "htmx_asset.hpp");

    std::string version = "2.0.11";
    fs::path toml = ctx.proj / "revet.toml";
    if (file_exists(toml)) {
        // `version = "..."` in the [htmx] section.
        Lines lines = splitlines(read_text(toml));
        auto start = std::ranges::find_if(lines, [](const std::string& l) -> bool {
            return static_cast<bool>(ctre::starts_with<R"RE(\s*\[htmx\])RE">(l));
        });
        if (start != lines.end()) {
            auto section =
                std::ranges::subrange(std::next(start), lines.end()) |
                std::views::take_while([](const std::string& l) -> bool { return !starts_with(strip(l), "["); });
            constexpr auto version_line = ctre::starts_with<R"RE(\s*version\s*=\s*"([^"]+)")RE">;
            auto line = std::ranges::find_if(
                section, [&](const std::string& l) -> bool { return static_cast<bool>(version_line(l)); });
            if (line != section.end()) {
                version = version_line(*line).get<1>().to_string();
            }
        }
    }

    fs::path vendor_file = ctx.framework / "vendor" / "htmx" / version / "htmx.min.js";
    if (!file_exists(vendor_file)) {
        throw GenError("gen_htmx: htmx " + version + " not vendored — run: scripts/vendor_htmx.sh");
    }

    Lines out = file_header({"#include <string_view>"});
    out.emplace_back("namespace revet::detail {");
    out.push_back(cat("inline constexpr std::string_view htmx_version = \"", version, "\";"));
    out.push_back(cat("inline constexpr std::string_view htmx_js = \"", escape_str(read_text(vendor_file)), "\";"));
    out.emplace_back("} // namespace revet::detail");
    out.emplace_back("");
    write_lines(out_path, out);
    return 0;
}

auto gen_optimistic(const Ctx& ctx) -> int {
    fs::path out_path = setup(ctx, "optimistic_asset.hpp");
    fs::path ts_file = ctx.framework / "static" / "optimistic.ts";
    if (!file_exists(ts_file)) {
        throw GenError("gen_optimistic: " + ts_file.string() + " not found");
    }

    fs::path bun = find_bun();
    if (bun.empty() || !file_exists(bun)) {
        throw GenError("gen_optimistic: bun not found");
    }

    fs::path tmp_dir = fs::temp_directory_path() /
                       ("revet-codegen-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp_dir);
    fs::path js_tmp = tmp_dir / "optimistic.js";
    fs::path err_tmp = tmp_dir / "bun.err";

    std::string cmd = cat(shell_quote(bun.string()), " build --minify ", shell_quote(ts_file.string()), " --outfile ",
                          shell_quote(js_tmp.string()), " 2>", shell_quote(err_tmp.string()));
    // NOLINTNEXTLINE(cert-env33-c,bugprone-command-processor): every argument is shell-quoted
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::string err = file_exists(err_tmp) ? read_text(err_tmp) : "";
        fs::remove_all(tmp_dir);
        throw GenError("gen_optimistic: bun build failed:\n" + err);
    }

    std::string js = read_text(js_tmp);
    fs::remove_all(tmp_dir);

    Lines out = file_header({"#include <string_view>"});
    out.emplace_back("namespace revet::detail {");
    out.push_back(cat("inline constexpr std::string_view optimistic_js = \"", escape_str(js), "\";"));
    out.emplace_back("} // namespace revet::detail");
    out.emplace_back("");
    write_lines(out_path, out);
    return 0;
}

} // namespace revet::codegen
