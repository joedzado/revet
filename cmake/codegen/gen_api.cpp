// [[revet::external("METHOD URL")]] declarations -> apis.hpp (inline HTTP coroutines).
#include <algorithm>
#include <map>
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
    std::vector<Param> params;
    for (const auto& raw : split_top_commas(s)) {
        auto m = ctre::match<R"RE(\s*(?:const\s+)?([\w:<>,\s]+?)\s+(\w+)\s*(?:=\s*([^\n]+?))?\s*)RE">(raw);
        if (!m) {
            continue;
        }
        params.push_back({.type = collapse_ws(strip(m.get<1>().to_view())), .name = m.get<2>().to_string()});
    }
    return params;
}

auto task_inner(const std::string& ret) -> std::string {
    std::string stripped = strip(ret);
    if (auto m = ctre::match<R"RE(\s*web::Task\s*<\s*([\s\S]+)\s*>\s*)RE">(stripped)) {
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
    for (auto m : ctre::search_all<R"RE(\bnamespace\s+(\w+)\s*\{)RE">(view)) {
        size_t pos = offset(m.get<0>().to_view());
        events.push_back({.pos = pos, .kind = 0, .payload = m.get<1>().to_string(), .end = pos + m.get<0>().size()});
    }
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '}') {
            events.push_back({.pos = i, .kind = 1, .payload = "", .end = i + 1});
        }
    }
    for (auto m : ctre::search_all<R"RE(\[\[\s*revet::external\s*\(\s*"([^"]+)"\s*\)\s*\]\])RE">(view)) {
        size_t pos = offset(m.get<0>().to_view());
        events.push_back({.pos = pos, .kind = 2, .payload = m.get<1>().to_string(), .end = pos + m.get<0>().size()});
    }
    std::ranges::stable_sort(events, [](const Ev& a, const Ev& b) -> bool { return a.pos < b.pos; });

    std::vector<External> out;
    std::vector<std::string> ns_stack;
    std::vector<int> ns_depths;
    int depth = 0;
    size_t last = 0;

    auto consume_braces = [&](size_t upto) -> void {
        for (size_t k = last; k < upto; ++k) {
            if (text[k] == '{') {
                ++depth;
            } else if (text[k] == '}') {
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
            auto mu = ctre::match<R"RE((GET|POST|PUT|DELETE|PATCH|HEAD|OPTIONS)\s+([^\n]+))RE">(method_url);
            if (!mu) {
                last = ev.end;
                continue;
            }
            std::string method = mu.get<1>().to_string();
            std::string url = strip(mu.get<2>().to_view());

            std::string tail = text.substr(ev.end, 2048);
            std::string lead = tail.substr(std::min(tail.find_first_not_of(" \t\r\n\f\v"), tail.size()));
            // Function declaration directly after the attribute:
            //   auto name(params) -> ReturnType;   |   ReturnType name(params);
            auto dm = ctre::starts_with<
                R"RE((?:inline\s+)?(?:auto\s+(\w+)\s*\(([^)]*)\)\s*->\s*([^;]+?)|([\w:<>,\s]+?)\s+(\w+)\s*\(([^)]*)\))\s*;)RE">(
                lead);
            if (!dm) {
                last = ev.end;
                continue;
            }
            bool alt1 = static_cast<bool>(dm.get<1>());
            std::string ret = strip(alt1 ? dm.get<3>().to_view() : dm.get<4>().to_view());
            std::string name = alt1 ? dm.get<1>().to_string() : dm.get<5>().to_string();
            std::string params_s = alt1 ? dm.get<2>().to_string() : dm.get<6>().to_string();

            std::string ns;
            for (const auto& n : ns_stack) {
                if (!ns.empty()) {
                    ns += "::";
                }
                ns += n;
            }
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
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

// Builds the URL expression: path params substituted, remaining params appended as a query string.
auto url_expr(const std::string& url, const std::vector<Param>& params) -> std::string {
    std::set<std::string> path_names;
    for (auto m : ctre::search_all<R"RE(\{(\w+)\})RE">(url)) {
        path_names.insert(m.get<1>().to_string());
    }

    std::string expr = cat("std::string(", json_str_literal(url), ")");
    for (const auto& p : params) {
        if (!path_names.contains(p.name)) {
            continue;
        }
        std::string val = to_string_expr(p.type, p.name);
        std::string key = "{" + p.name + "}";
        expr = cat("[](std::string s, const std::string& v) { auto p = s.find(\"", key,
                   "\"); if (p != std::string::npos) s.replace(p, ", std::to_string(p.name.size() + 2),
                   ", v); return s; }(", expr, ", ", val, ")");
    }
    bool first_q = !url.contains('?');
    for (const auto& p : params) {
        if (path_names.contains(p.name)) {
            continue;
        }
        std::string val = to_string_expr(p.type, p.name);
        std::string sep = first_q ? "?" : "&";
        first_q = false;
        expr = cat(expr, " + \"", sep, p.name, "=\" + ", val);
    }
    return expr;
}

auto http_helper(const std::string& method) -> std::string {
    std::string m = lower(method);
    return m == "delete" ? "delete_" : m;
}

auto emit_definition(const External& e) -> Lines {
    std::string sig;
    for (size_t i = 0; i < e.params.size(); ++i) {
        if (i != 0U) {
            sig += ", ";
        }
        sig += e.params[i].type + " " + e.params[i].name;
    }
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
    fs::path out_path = setup(ctx, "apis.hpp");
    fs::path apis_dir = ctx.proj / "app" / "apis";

    std::vector<External> externs;
    for (const auto& f : glob(apis_dir, ".hpp")) {
        for (auto& e : scan_file(f)) {
            externs.push_back(std::move(e));
        }
    }

    Lines lines = file_header({"#include <coroutine>", "#include <revet/task.hpp>", "#include <revet/http.hpp>",
                               "#include <revet/json.hpp>", "#include \"models.hpp\"", "#include <string>"});
    for (const auto& hpp : glob(apis_dir, ".hpp")) {
        lines.push_back(rel_include(hpp, ctx.proj));
    }
    lines.emplace_back("");

    // Group by namespace, emitted in sorted namespace order.
    std::map<std::string, std::vector<External>> by_ns;
    for (auto& e : externs) {
        by_ns[e.ns].push_back(std::move(e));
    }
    for (const auto& [ns, group] : by_ns) {
        size_t depth = 0;
        if (!ns.empty()) {
            std::string opened;
            size_t start = 0;
            while (start <= ns.size()) {
                size_t end = ns.find("::", start);
                if (end == std::string::npos) {
                    end = ns.size();
                }
                if (depth != 0U) {
                    opened += ' ';
                }
                opened += "namespace " + ns.substr(start, end - start) + " {";
                ++depth;
                start = end + 2;
            }
            lines.push_back(opened);
        }
        for (const auto& e : group) {
            for (const auto& l : emit_definition(e)) {
                lines.push_back(l);
            }
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
