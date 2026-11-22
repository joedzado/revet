// [[revet::external("METHOD URL")]] declarations -> apis.hpp (inline HTTP coroutines).
#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <ranges>
#include <set>

#include "common.hpp"

namespace revet::codegen {
namespace {

struct Param {
    std::string type;
    std::string name;
};

struct External {
    std::string method;
    std::string url;
    std::string ns; // "a::b" or empty
    std::string func_name;
    std::string return_type;
    std::string response_type;
    std::vector<Param> params;
};

// Splits on commas outside <>, (), [] and drops empties.
auto split_top_commas(const std::string& s) -> Lines {
    return split_args(s);
}

auto parse_params(const std::string& s) -> std::vector<Param> {
    constexpr auto param = ctre::match<R"RE(\s*(?:const\s+)?([\w:<>,\s]+?)\s+(\w+)\s*(?:=\s*([^\n]+?))?\s*)RE">;
    return split_top_commas(s) |
           std::views::filter([&](const std::string& raw) -> bool { return static_cast<bool>(param(raw)); }) |
           std::views::transform([&](const std::string& raw) -> Param {
               const auto m = param(raw);
               return {.type = collapse_ws(strip(m.get<1>().to_view())), .name = m.get<2>().to_string()};
           }) |
           std::ranges::to<std::vector<Param>>();
}

auto task_inner(const std::string& ret) -> std::string {
    std::string stripped = strip(ret);
    if (const auto m = ctre::match<R"RE(\s*web::Task\s*<\s*([\s\S]+)\s*>\s*)RE">(stripped)) {
        return strip(m.get<1>().to_view());
    }
    return stripped;
}

// Namespace tracking: a brace walk over the whole text.
auto scan_file(const fs::path& path) -> std::vector<External> {
    std::string text = read_text(path);

    struct Ev {
        size_t pos;
        int kind;            // 0 = namespace open, 1 = closing brace, 2 = attribute
        std::string payload; // namespace name, or attribute method+url
        size_t end;
    };
    std::vector<Ev> events;
    const std::string_view view = text;
    auto offset = [&](std::string_view part) -> size_t { return static_cast<size_t>(part.data() - view.data()); };
    const auto event_of = [&](int kind) -> auto {
        return [&, kind](const auto& m) -> Ev {
            const size_t pos = offset(m.template get<0>().to_view());
            return {.pos = pos,
                    .kind = kind,
                    .payload = m.template get<1>().to_string(),
                    .end = pos + m.template get<0>().size()};
        };
    };
    std::ranges::transform(ctre::search_all<R"RE(\bnamespace\s+(\w+)\s*\{)RE">(view), std::back_inserter(events),
                           event_of(0));
    std::ranges::transform(
        std::views::iota(0UZ, text.size()) | std::views::filter([&](size_t i) -> bool { return text.at(i) == '}'; }),
        std::back_inserter(events), [](size_t i) -> Ev { return {.pos = i, .kind = 1, .payload = "", .end = i + 1}; });
    std::ranges::transform(ctre::search_all<R"RE(\[\[\s*revet::external\s*\(\s*"([^"]+)"\s*\)\s*\]\])RE">(view),
                           std::back_inserter(events), event_of(2));
    std::ranges::stable_sort(events, [](const Ev& a, const Ev& b) -> bool { return a.pos < b.pos; });

    std::vector<External> out;
    std::vector<std::string> ns_stack;
    std::vector<int> ns_depths;
    int depth = 0;
    size_t last = 0;

    const auto consume_braces = [&](size_t upto) -> void {
        for (size_t k = last; k < upto; ++k) {
            if (text.at(k) == '{') {
                ++depth;
            } else if (text.at(k) == '}') {
                --depth;
                while (!ns_depths.empty() && depth < ns_depths.back()) {
                    ns_depths.pop_back();
                    ns_stack.pop_back();
                }
            }
        }
    };

    for (const auto& ev : events) {
        consume_braces(ev.pos);
        if (ev.kind == 0) {
            ++depth;
            ns_stack.push_back(ev.payload);
            ns_depths.push_back(depth);
            last = ev.end;
        } else if (ev.kind == 1) {
            last = ev.pos;
        } else {
            std::string method_url = strip(ev.payload);
            const auto mu = ctre::match<R"RE((GET|POST|PUT|DELETE|PATCH|HEAD|OPTIONS)\s+([^\n]+))RE">(method_url);
            if (!mu) {
                last = ev.end;
                continue;
            }
            const std::string method = mu.get<1>().to_string();
            const std::string url = strip(mu.get<2>().to_view());

            const std::string tail = text.substr(ev.end, 2048);
            std::string lead = tail.substr(std::min(tail.find_first_not_of(" \t\r\n\f\v"), tail.size()));
            // Function declaration directly after the attribute:
            //   auto name(params) -> ReturnType;   |   ReturnType name(params);
            const auto dm = ctre::starts_with<
                R"RE((?:inline\s+)?(?:auto\s+(\w+)\s*\(([^)]*)\)\s*->\s*([^;]+?)|([\w:<>,\s]+?)\s+(\w+)\s*\(([^)]*)\))\s*;)RE">(
                lead);
            if (!dm) {
                last = ev.end;
                continue;
            }
            const bool alt1 = static_cast<bool>(dm.get<1>());
            const std::string ret = strip(alt1 ? dm.get<3>().to_view() : dm.get<4>().to_view());
            const std::string name = alt1 ? dm.get<1>().to_string() : dm.get<5>().to_string();
            const std::string params_s = alt1 ? dm.get<2>().to_string() : dm.get<6>().to_string();

            const std::string ns = join(ns_stack, "::");
            out.push_back({.method = method,
                           .url = url,
                           .ns = ns,
                           .func_name = name,
                           .return_type = ret,
                           .response_type = task_inner(ret),
                           .params = parse_params(params_s)});
            last = ev.end;
        }
    }
    return out;
}

auto to_string_expr(const std::string& ptype, const std::string& name) -> std::string {
    if (lower(ptype).contains("string")) {
        return cat("std::string(", name, ")");
    }
    return cat("std::to_string(", name, ")");
}

auto json_str_literal(const std::string& s) -> std::string {
    return "\"" + escape_quotes(s) + "\"";
}

// Builds the URL expression: path params substituted, remaining params appended as a query string.
auto url_expr(const std::string& url, const std::vector<Param>& params) -> std::string {
    Lines path_list = captures<R"RE(\{(\w+)\})RE">(url);
    std::set<std::string> path_names(path_list.begin(), path_list.end());
    auto in_path = [&](const Param& p) -> bool { return path_names.contains(p.name); };

    // Each path param wraps the expression in a call that replaces its {name}.
    const std::string expr = std::ranges::fold_left(
        params | std::views::filter(in_path), cat("std::string(", json_str_literal(url), ")"),
        [](const std::string& acc, const Param& p) -> std::string {
            return cat("[](std::string s, const std::string& v) { auto p = s.find(\"{", p.name,
                       "}\"); if (p != std::string::npos) s.replace(p, ", std::to_string(p.name.size() + 2),
                       ", v); return s; }(", acc, ", ", to_string_expr(p.type, p.name), ")");
        });
    // The rest become the query string.
    auto query = params | std::views::filter(std::not_fn(in_path)) | std::ranges::to<std::vector<Param>>();
    bool has_query = url.contains('?');
    return std::ranges::fold_left(std::views::zip(std::views::iota(0UZ), query), expr,
                                  [&](const std::string& acc, const auto& indexed) -> std::string {
                                      const auto& [i, p] = indexed;
                                      const std::string_view sep = i == 0 && !has_query ? "?" : "&";
                                      return cat(acc, " + \"", sep, p.name, "=\" + ", to_string_expr(p.type, p.name));
                                  });
}

auto http_helper(const std::string& method) -> std::string {
    const std::string m = lower(method);
    return m == "delete" ? "delete_" : m;
}

auto emit_definition(const External& e) -> Lines {
    const std::string sig = join(e.params | std::views::transform([](const Param& p) -> std::string {
                                     return p.type + " " + p.name;
                                 }) | std::ranges::to<Lines>(),
                                 ", ");
    Lines lines;
    lines.push_back(cat("inline auto ", e.func_name, "(", sig, ") -> ", e.return_type, " {"));
    lines.push_back(cat("    auto url  = ", url_expr(e.url, e.params), ";"));
    lines.push_back(cat("    auto resp = co_await http::", http_helper(e.method), "(url);"));
    lines.emplace_back("    if (!resp.ok()) throw http::Error(resp.status_code, resp.body);");
    lines.emplace_back("    revet::json::Document doc(resp.body);");
    lines.push_back(cat("    co_return revet::json::from_json<", e.response_type, ">(doc);"));
    lines.emplace_back("}");
    return lines;
}

} // namespace

auto gen_api(const Ctx& ctx) -> int {
    const fs::path out_path = setup(ctx, "apis.hpp");
    const fs::path apis_dir = ctx.proj / "app" / "apis";

    auto externs = glob(apis_dir, ".hpp") | std::views::transform(scan_file) | std::views::join |
                   std::ranges::to<std::vector<External>>();

    Lines lines = file_header({"#include <coroutine>", "#include <revet/task.hpp>", "#include <revet/http.hpp>",
                               "#include <revet/json.hpp>", "#include \"models.hpp\"", "#include <string>"});
    std::ranges::transform(glob(apis_dir, ".hpp"), std::back_inserter(lines),
                           [&](const fs::path& hpp) -> std::string { return rel_include(hpp, ctx.proj); });
    lines.emplace_back("");

    // Group by namespace, emitted in sorted namespace order.
    std::map<std::string, std::vector<External>> by_ns;
    for (auto& e : externs) {
        by_ns[e.ns].push_back(std::move(e));
    }
    for (const auto& [ns, group] : by_ns) {
        const auto opens = ns | std::views::split(std::string_view("::")) |
                           std::views::transform([](auto part) -> std::string {
                               return "namespace " + std::string(std::string_view(part)) + " {";
                           }) |
                           std::ranges::to<Lines>();
        const size_t depth = opens.size();
        if (!ns.empty()) {
            lines.push_back(join(opens, " "));
        }
        for (const auto& e : group) {
            append(lines, emit_definition(e));
            lines.emplace_back("");
        }
        if (!ns.empty()) {
            lines.push_back(std::string(depth, '}') + " // namespace " + ns);
            lines.emplace_back("");
        }
    }

    write_file(out_path, join(lines, "\n"));
    return 0;
}

} // namespace revet::codegen
